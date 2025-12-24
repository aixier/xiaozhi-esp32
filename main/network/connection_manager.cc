#include "connection_manager.h"

#include <esp_log.h>

static const char* TAG = "ConnectionMgr";

ConnectionManager& ConnectionManager::GetInstance() {
    static ConnectionManager instance;
    return instance;
}

ConnectionManager::ConnectionManager() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }

    // 创建心跳定时器
    esp_timer_create_args_t heartbeat_args = {
        .callback = [](void* arg) {
            static_cast<ConnectionManager*>(arg)->OnHeartbeatTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "heartbeat",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&heartbeat_args, &heartbeat_timer_);

    // 创建重连定时器
    esp_timer_create_args_t reconnect_args = {
        .callback = [](void* arg) {
            static_cast<ConnectionManager*>(arg)->OnReconnectTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "reconnect",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&reconnect_args, &reconnect_timer_);
}

ConnectionManager::~ConnectionManager() {
    StopHeartbeat();

    if (heartbeat_timer_) {
        esp_timer_delete(heartbeat_timer_);
    }
    if (reconnect_timer_) {
        esp_timer_delete(reconnect_timer_);
    }
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

void ConnectionManager::Initialize(const Callbacks& callbacks) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        callbacks_ = callbacks;
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Initialized");
}

void ConnectionManager::Connect() {
    if (state_ == CONNECTING || state_ == CONNECTED) {
        ESP_LOGW(TAG, "Already connecting or connected");
        return;
    }

    user_disconnected_ = false;
    reconnect_count_ = 0;

    SetState(CONNECTING);

    // 发布连接开始事件
    ConnectionEvent event(EventType::CONN_STARTING);
    EventBus::GetInstance().Emit(event);

    Callbacks cb;
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        cb = callbacks_;
        xSemaphoreGive(mutex_);
    }

    if (cb.on_connect) {
        bool success = cb.on_connect();
        if (!success) {
            OnError(-1, "Connection failed");
        }
    }
}

void ConnectionManager::Disconnect() {
    ESP_LOGI(TAG, "User disconnect requested");
    user_disconnected_ = true;

    StopHeartbeat();

    if (reconnect_timer_) {
        esp_timer_stop(reconnect_timer_);
    }

    Callbacks cb;
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        cb = callbacks_;
        xSemaphoreGive(mutex_);
    }

    if (cb.on_disconnect) {
        cb.on_disconnect();
    }

    SetState(DISCONNECTED);

    ConnectionEvent event(EventType::CONN_DISCONNECTED);
    EventBus::GetInstance().Emit(event);
}

ConnectionManager::State ConnectionManager::GetState() const {
    return state_;
}

void ConnectionManager::OnConnected() {
    ESP_LOGI(TAG, "Connection established");

    reconnect_count_ = 0;
    SetState(CONNECTED);

    StartHeartbeat();

    // 发布连接成功事件
    ConnectionEvent event(EventType::CONN_SUCCESS);
    EventBus::GetInstance().Emit(event);
}

void ConnectionManager::OnDisconnected() {
    ESP_LOGW(TAG, "Connection lost");

    StopHeartbeat();

    if (user_disconnected_) {
        SetState(DISCONNECTED);
        return;
    }

    // 尝试重连
    AttemptReconnect();
}

void ConnectionManager::OnPongReceived() {
    pong_received_ = true;
    last_pong_time_ = esp_timer_get_time() / 1000;
    ESP_LOGD(TAG, "Pong received");
}

void ConnectionManager::OnError(int code, const std::string& message) {
    ESP_LOGE(TAG, "Connection error: %d - %s", code, message.c_str());

    StopHeartbeat();

    if (state_ == CONNECTING) {
        // 首次连接失败
        ConnectionEvent event(EventType::CONN_FAILED);
        event.error_code = code;
        event.error_message = message;
        EventBus::GetInstance().Emit(event);

        // 尝试重连
        AttemptReconnect();
    } else if (state_ == CONNECTED) {
        // 已连接状态下发生错误
        AttemptReconnect();
    }
}

int ConnectionManager::GetReconnectCount() const {
    return reconnect_count_;
}

void ConnectionManager::StartHeartbeat() {
    if (heartbeat_timer_) {
        pong_received_ = true;
        last_pong_time_ = esp_timer_get_time() / 1000;
        esp_timer_start_periodic(heartbeat_timer_, HEARTBEAT_INTERVAL_MS * 1000);
        ESP_LOGI(TAG, "Heartbeat started (interval: %dms)", HEARTBEAT_INTERVAL_MS);
    }
}

void ConnectionManager::StopHeartbeat() {
    if (heartbeat_timer_) {
        esp_timer_stop(heartbeat_timer_);
        ESP_LOGD(TAG, "Heartbeat stopped");
    }
}

void ConnectionManager::OnHeartbeatTimer() {
    if (state_ != CONNECTED) {
        return;
    }

    // 检查上次 Pong 是否超时
    if (!pong_received_) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t elapsed = now - last_pong_time_;

        if (elapsed > HEARTBEAT_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Heartbeat timeout (elapsed: %lld ms)", elapsed);

            // 发布心跳超时事件
            ConnectionEvent event(EventType::CONN_HEARTBEAT_TIMEOUT);
            EventBus::GetInstance().Emit(event);

            // 触发重连
            OnDisconnected();
            return;
        }
    }

    // 发送 Ping
    pong_received_ = false;

    Callbacks cb;
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        cb = callbacks_;
        xSemaphoreGive(mutex_);
    }

    if (cb.on_send_ping) {
        cb.on_send_ping();
    }
}

void ConnectionManager::AttemptReconnect() {
    if (user_disconnected_) {
        ESP_LOGI(TAG, "User disconnected, not reconnecting");
        SetState(DISCONNECTED);
        return;
    }

    if (reconnect_count_ >= RECONNECT_MAX_ATTEMPTS) {
        ESP_LOGE(TAG, "Max reconnect attempts reached (%d)", RECONNECT_MAX_ATTEMPTS);

        SetState(DISCONNECTED);

        ConnectionEvent event(EventType::CONN_FAILED);
        event.error_code = -1;
        event.error_message = "Max reconnect attempts reached";
        event.retry_count = reconnect_count_;
        EventBus::GetInstance().Emit(event);

        return;
    }

    SetState(RECONNECTING);

    int delay = GetReconnectDelay();
    ESP_LOGI(TAG, "Reconnecting in %d ms (attempt %d/%d)",
             delay, reconnect_count_ + 1, RECONNECT_MAX_ATTEMPTS);

    // 发布重连事件
    ConnectionEvent event(EventType::CONN_RECONNECTING);
    event.retry_count = reconnect_count_;
    EventBus::GetInstance().Emit(event);

    // 启动重连定时器
    if (reconnect_timer_) {
        esp_timer_start_once(reconnect_timer_, delay * 1000);
    }
}

void ConnectionManager::OnReconnectTimer() {
    if (state_ != RECONNECTING) {
        return;
    }

    reconnect_count_++;

    ESP_LOGI(TAG, "Reconnect attempt %d/%d", reconnect_count_.load(), RECONNECT_MAX_ATTEMPTS);

    Callbacks cb;
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        cb = callbacks_;
        xSemaphoreGive(mutex_);
    }

    if (cb.on_connect) {
        bool success = cb.on_connect();
        if (!success) {
            // 重连失败，继续尝试
            AttemptReconnect();
        }
    }
}

int ConnectionManager::GetReconnectDelay() const {
    // 指数退避: 1s, 2s, 4s, 8s, 16s, 30s (capped)
    int delay = RECONNECT_DELAY_INITIAL_MS * (1 << reconnect_count_);
    if (delay > RECONNECT_DELAY_MAX_MS) {
        delay = RECONNECT_DELAY_MAX_MS;
    }
    return delay;
}

void ConnectionManager::SetState(State new_state) {
    if (state_ == new_state) {
        return;
    }

    const char* state_names[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "RECONNECTING"};
    ESP_LOGI(TAG, "State: %s -> %s", state_names[state_], state_names[new_state]);

    state_ = new_state;
}
