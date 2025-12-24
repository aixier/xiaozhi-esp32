#include "display_engine.h"
#include "display.h"

#include <esp_log.h>

static const char* TAG = "DisplayEngine";

DisplayEngine::DisplayEngine() {
    // 创建省电定时器
    esp_timer_create_args_t idle_args = {
        .callback = [](void* arg) {
            static_cast<DisplayEngine*>(arg)->OnIdleTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_idle",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&idle_args, &idle_timer_);

    // 创建恢复定时器
    esp_timer_create_args_t restore_args = {
        .callback = [](void* arg) {
            static_cast<DisplayEngine*>(arg)->OnRestoreTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_restore",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&restore_args, &restore_timer_);

    // 设置表情状态回调
    EmotionState::Callbacks emotion_cbs;
    emotion_cbs.on_emotion_change = [this](const std::string& emotion) {
        if (callbacks_.set_emotion) {
            callbacks_.set_emotion(emotion);
        }
    };
    emotion_state_.SetCallbacks(emotion_cbs);
}

DisplayEngine::~DisplayEngine() {
    UnsubscribeEvents();

    if (idle_timer_) {
        esp_timer_stop(idle_timer_);
        esp_timer_delete(idle_timer_);
    }
    if (restore_timer_) {
        esp_timer_stop(restore_timer_);
        esp_timer_delete(restore_timer_);
    }
}

void DisplayEngine::Initialize(Display* display) {
    display_ = display;

    SubscribeEvents();

    // 启动省电定时器
    OnUserActivity();

    ESP_LOGI(TAG, "Initialized");
}

void DisplayEngine::SetCallbacks(const Callbacks& callbacks) {
    callbacks_ = callbacks;
}

EmotionState& DisplayEngine::GetEmotionState() {
    return emotion_state_;
}

void DisplayEngine::OnUserActivity() {
    // 唤醒显示
    if (power_mode_ != POWER_NORMAL) {
        SetPowerMode(POWER_NORMAL);
    }

    // 重置省电定时器
    if (idle_timer_) {
        esp_timer_stop(idle_timer_);
        esp_timer_start_once(idle_timer_, DIM_TIMEOUT_MS * 1000);
    }
}

DisplayEngine::PowerMode DisplayEngine::GetPowerMode() const {
    return power_mode_;
}

void DisplayEngine::SetPowerMode(PowerMode mode) {
    if (power_mode_ == mode) {
        return;
    }

    const char* mode_names[] = {"NORMAL", "DIM", "SLEEP"};
    ESP_LOGI(TAG, "Power mode: %s -> %s",
             mode_names[power_mode_], mode_names[mode]);

    power_mode_ = mode;

    int brightness = 100;
    switch (mode) {
        case POWER_NORMAL:
            brightness = 100;
            break;
        case POWER_DIM:
            brightness = 30;
            break;
        case POWER_SLEEP:
            brightness = 0;
            emotion_state_.SetEmotion("sleepy");
            break;
    }

    if (callbacks_.set_brightness) {
        callbacks_.set_brightness(brightness);
    }
}

void DisplayEngine::SubscribeEvents() {
    auto& bus = EventBus::GetInstance();

    sub_conn_starting_ = bus.Subscribe(
        EventType::CONN_STARTING,
        [this](const Event& e) { OnConnectionStarting(e); }
    );

    sub_conn_success_ = bus.Subscribe(
        EventType::CONN_SUCCESS,
        [this](const Event& e) { OnConnectionSuccess(e); }
    );

    sub_conn_failed_ = bus.Subscribe(
        EventType::CONN_FAILED,
        [this](const Event& e) { OnConnectionFailed(e); }
    );

    sub_conn_disconnected_ = bus.Subscribe(
        EventType::CONN_DISCONNECTED,
        [this](const Event& e) { OnConnectionDisconnected(e); }
    );

    sub_conn_reconnecting_ = bus.Subscribe(
        EventType::CONN_RECONNECTING,
        [this](const Event& e) { OnConnectionReconnecting(e); }
    );

    sub_audio_started_ = bus.Subscribe(
        EventType::AUDIO_PLAYBACK_STARTED,
        [this](const Event& e) { OnAudioPlaybackStarted(e); }
    );

    sub_audio_complete_ = bus.Subscribe(
        EventType::AUDIO_PLAYBACK_COMPLETE,
        [this](const Event& e) { OnAudioPlaybackComplete(e); }
    );

    sub_display_emotion_ = bus.Subscribe(
        EventType::DISPLAY_SET_EMOTION,
        [this](const Event& e) { OnDisplaySetEmotion(e); }
    );

    sub_display_text_ = bus.Subscribe(
        EventType::DISPLAY_SET_TEXT,
        [this](const Event& e) { OnDisplaySetText(e); }
    );

    sub_display_status_ = bus.Subscribe(
        EventType::DISPLAY_SET_STATUS,
        [this](const Event& e) { OnDisplaySetStatus(e); }
    );

    sub_system_error_ = bus.Subscribe(
        EventType::SYSTEM_ERROR,
        [this](const Event& e) { OnSystemError(e); }
    );

    sub_system_idle_ = bus.Subscribe(
        EventType::SYSTEM_IDLE_TIMEOUT,
        [this](const Event& e) { OnSystemIdleTimeout(e); }
    );

    sub_user_button_ = bus.Subscribe(
        EventType::USER_BUTTON_PRESSED,
        [this](const Event& e) { OnUserButtonPressed(e); }
    );

    sub_user_wake_ = bus.Subscribe(
        EventType::USER_WAKE_WORD,
        [this](const Event& e) { OnUserWakeWord(e); }
    );

    ESP_LOGD(TAG, "Subscribed to events");
}

void DisplayEngine::UnsubscribeEvents() {
    auto& bus = EventBus::GetInstance();

    if (sub_conn_starting_ >= 0) bus.Unsubscribe(EventType::CONN_STARTING, sub_conn_starting_);
    if (sub_conn_success_ >= 0) bus.Unsubscribe(EventType::CONN_SUCCESS, sub_conn_success_);
    if (sub_conn_failed_ >= 0) bus.Unsubscribe(EventType::CONN_FAILED, sub_conn_failed_);
    if (sub_conn_disconnected_ >= 0) bus.Unsubscribe(EventType::CONN_DISCONNECTED, sub_conn_disconnected_);
    if (sub_conn_reconnecting_ >= 0) bus.Unsubscribe(EventType::CONN_RECONNECTING, sub_conn_reconnecting_);
    if (sub_audio_started_ >= 0) bus.Unsubscribe(EventType::AUDIO_PLAYBACK_STARTED, sub_audio_started_);
    if (sub_audio_complete_ >= 0) bus.Unsubscribe(EventType::AUDIO_PLAYBACK_COMPLETE, sub_audio_complete_);
    if (sub_display_emotion_ >= 0) bus.Unsubscribe(EventType::DISPLAY_SET_EMOTION, sub_display_emotion_);
    if (sub_display_text_ >= 0) bus.Unsubscribe(EventType::DISPLAY_SET_TEXT, sub_display_text_);
    if (sub_display_status_ >= 0) bus.Unsubscribe(EventType::DISPLAY_SET_STATUS, sub_display_status_);
    if (sub_system_error_ >= 0) bus.Unsubscribe(EventType::SYSTEM_ERROR, sub_system_error_);
    if (sub_system_idle_ >= 0) bus.Unsubscribe(EventType::SYSTEM_IDLE_TIMEOUT, sub_system_idle_);
    if (sub_user_button_ >= 0) bus.Unsubscribe(EventType::USER_BUTTON_PRESSED, sub_user_button_);
    if (sub_user_wake_ >= 0) bus.Unsubscribe(EventType::USER_WAKE_WORD, sub_user_wake_);

    ESP_LOGD(TAG, "Unsubscribed from events");
}

void DisplayEngine::OnConnectionStarting(const Event& e) {
    ESP_LOGD(TAG, "Connection starting");
    OnUserActivity();
    emotion_state_.SetEmotion("thinking");
    if (callbacks_.set_status) {
        callbacks_.set_status("连接中...");
    }
}

void DisplayEngine::OnConnectionSuccess(const Event& e) {
    ESP_LOGD(TAG, "Connection success");
    OnUserActivity();
    emotion_state_.SetEmotion("neutral");
    if (callbacks_.set_status) {
        callbacks_.set_status("已连接");
    }
}

void DisplayEngine::OnConnectionFailed(const Event& e) {
    ESP_LOGD(TAG, "Connection failed");
    OnUserActivity();

    const auto* conn = static_cast<const ConnectionEvent*>(&e);
    emotion_state_.SetEmotion("sad");

    if (callbacks_.set_status) {
        std::string status = "连接失败";
        if (!conn->error_message.empty()) {
            status += ": " + conn->error_message;
        }
        callbacks_.set_status(status);
    }
}

void DisplayEngine::OnConnectionDisconnected(const Event& e) {
    ESP_LOGD(TAG, "Connection disconnected");
    emotion_state_.SetEmotion("confused");
    if (callbacks_.set_status) {
        callbacks_.set_status("已断开");
    }
}

void DisplayEngine::OnConnectionReconnecting(const Event& e) {
    ESP_LOGD(TAG, "Connection reconnecting");
    const auto* conn = static_cast<const ConnectionEvent*>(&e);
    emotion_state_.SetEmotion("thinking");
    if (callbacks_.set_status) {
        char status[64];
        snprintf(status, sizeof(status), "重连中 (%d)", conn->retry_count + 1);
        callbacks_.set_status(status);
    }
}

void DisplayEngine::OnAudioPlaybackStarted(const Event& e) {
    ESP_LOGD(TAG, "Audio playback started");
    OnUserActivity();
    // 表情由 DISPLAY_SET_EMOTION 事件设置
}

void DisplayEngine::OnAudioPlaybackComplete(const Event& e) {
    ESP_LOGD(TAG, "Audio playback complete");

    // 延迟恢复到 neutral
    if (restore_timer_) {
        esp_timer_stop(restore_timer_);
        esp_timer_start_once(restore_timer_, RESTORE_DELAY_MS * 1000);
    }
}

void DisplayEngine::OnDisplaySetEmotion(const Event& e) {
    const auto* disp = static_cast<const DisplayEvent*>(&e);
    ESP_LOGI(TAG, ">>> OnDisplaySetEmotion: %s", disp->emotion.c_str());
    OnUserActivity();

    if (!disp->emotion.empty()) {
        emotion_state_.TransitionTo(disp->emotion);
        // Switch to Emotion Mode
        if (display_) {
            display_->SetDisplayMode(kDisplayModeEmotion);
        }
    }
}

void DisplayEngine::OnDisplaySetText(const Event& e) {
    const auto* disp = static_cast<const DisplayEvent*>(&e);
    ESP_LOGD(TAG, "Set text: %s (%s)", disp->text.c_str(), disp->role.c_str());
    OnUserActivity();

    if (callbacks_.set_chat_message) {
        callbacks_.set_chat_message(disp->text);
    }
    
    // Switch to Chat Mode
    if (display_) {
        display_->SetDisplayMode(kDisplayModeChat);
    }
}

void DisplayEngine::OnDisplaySetStatus(const Event& e) {
    const auto* disp = static_cast<const DisplayEvent*>(&e);
    ESP_LOGD(TAG, "Set status: %s", disp->text.c_str());

    if (callbacks_.set_status) {
        callbacks_.set_status(disp->text);
    }
}

void DisplayEngine::OnSystemError(const Event& e) {
    const auto* err = static_cast<const ErrorEvent*>(&e);
    ESP_LOGE(TAG, "System error: %d - %s", err->code, err->message.c_str());
    OnUserActivity();

    // 根据错误类型选择表情
    if (err->category == "network") {
        emotion_state_.SetEmotion("confused");
    } else {
        emotion_state_.SetEmotion("sad");
    }

    if (callbacks_.set_status) {
        callbacks_.set_status(err->message);
    }
}

void DisplayEngine::OnSystemIdleTimeout(const Event& e) {
    ESP_LOGD(TAG, "System idle timeout");
    SetPowerMode(POWER_SLEEP);
}

void DisplayEngine::OnUserButtonPressed(const Event& e) {
    ESP_LOGD(TAG, "User button pressed");
    OnUserActivity();
}

void DisplayEngine::OnUserWakeWord(const Event& e) {
    ESP_LOGD(TAG, "User wake word");
    OnUserActivity();
    emotion_state_.SetEmotion("happy");
}

void DisplayEngine::OnIdleTimer() {
    ESP_LOGD(TAG, "Idle timer fired");

    if (power_mode_ == POWER_NORMAL) {
        // 先变暗
        SetPowerMode(POWER_DIM);

        // 继续计时进入睡眠
        if (idle_timer_) {
            esp_timer_start_once(idle_timer_, (SLEEP_TIMEOUT_MS - DIM_TIMEOUT_MS) * 1000);
        }
    } else if (power_mode_ == POWER_DIM) {
        // 进入睡眠
        SetPowerMode(POWER_SLEEP);

        // 发布空闲超时事件
        Event event(EventType::SYSTEM_IDLE_TIMEOUT);
        EventBus::GetInstance().Emit(event);
    }
}

void DisplayEngine::OnRestoreTimer() {
    ESP_LOGD(TAG, "Restore timer fired, returning to neutral");
    emotion_state_.SetEmotion("neutral");
}
