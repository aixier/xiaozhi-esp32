#include "event_bus.h"

#include <esp_log.h>
#include <algorithm>
#include <cstring>

static const char* TAG = "EventBus";

EventBus& EventBus::GetInstance() {
    static EventBus instance;
    return instance;
}

EventBus::EventBus() {
    mutex_ = xSemaphoreCreateMutex();
    event_queue_ = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(QueuedEvent));

    if (mutex_ == nullptr || event_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex or queue");
    }
}

EventBus::~EventBus() {
    StopEventLoop();

    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
    if (event_queue_) {
        vQueueDelete(event_queue_);
    }
}

int EventBus::Subscribe(EventType type, EventHandler handler, Priority priority) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return -1;
    }

    int id = next_id_++;

    Subscriber sub;
    sub.id = id;
    sub.handler = handler;
    sub.priority = priority;

    auto& subs = subscribers_[type];
    subs.push_back(sub);

    // 按优先级排序 (高优先级在前)
    std::sort(subs.begin(), subs.end(), [](const Subscriber& a, const Subscriber& b) {
        return a.priority > b.priority;
    });

    xSemaphoreGive(mutex_);

    ESP_LOGD(TAG, "Subscribe: type=%d, id=%d, priority=%d",
             static_cast<int>(type), id, priority);

    return id;
}

void EventBus::Unsubscribe(EventType type, int handler_id) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return;
    }

    auto it = subscribers_.find(type);
    if (it != subscribers_.end()) {
        auto& subs = it->second;
        subs.erase(
            std::remove_if(subs.begin(), subs.end(),
                [handler_id](const Subscriber& s) { return s.id == handler_id; }),
            subs.end()
        );

        // 如果没有订阅者了，移除整个条目
        if (subs.empty()) {
            subscribers_.erase(it);
        }
    }

    xSemaphoreGive(mutex_);

    ESP_LOGD(TAG, "Unsubscribe: type=%d, id=%d", static_cast<int>(type), handler_id);
}

void EventBus::Emit(const Event& event) {
    // 复制订阅者列表，避免在回调中修改
    std::vector<Subscriber> subs_copy;

    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        auto it = subscribers_.find(event.type);
        if (it != subscribers_.end()) {
            subs_copy = it->second;
        }
        xSemaphoreGive(mutex_);
    }

    // 在锁外执行回调
    for (const auto& sub : subs_copy) {
        try {
            sub.handler(event);
        } catch (...) {
            ESP_LOGE(TAG, "Exception in event handler: type=%d, id=%d",
                     static_cast<int>(event.type), sub.id);
        }
    }
}

bool EventBus::EmitAsync(const Event& event) {
    if (event_queue_ == nullptr) {
        return false;
    }

    QueuedEvent qe;
    memset(&qe, 0, sizeof(qe));
    qe.type = event.type;
    qe.timestamp = event.timestamp;

    // 根据事件类型复制数据
    if (event.type == EventType::SYSTEM_ERROR) {
        const auto* err = static_cast<const ErrorEvent*>(&event);
        qe.error_code = err->code;
        strncpy(qe.message, err->message.c_str(), sizeof(qe.message) - 1);
    } else if (event.type == EventType::DISPLAY_SET_EMOTION) {
        const auto* disp = static_cast<const DisplayEvent*>(&event);
        strncpy(qe.emotion, disp->emotion.c_str(), sizeof(qe.emotion) - 1);
    } else if (event.type == EventType::DISPLAY_SET_TEXT) {
        const auto* disp = static_cast<const DisplayEvent*>(&event);
        strncpy(qe.text, disp->text.c_str(), sizeof(qe.text) - 1);
    } else if (event.type >= EventType::CONN_STARTING &&
               event.type <= EventType::CONN_HEARTBEAT_TIMEOUT) {
        const auto* conn = static_cast<const ConnectionEvent*>(&event);
        qe.error_code = conn->error_code;
        strncpy(qe.message, conn->error_message.c_str(), sizeof(qe.message) - 1);
    }

    if (xQueueSend(event_queue_, &qe, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type=%d",
                 static_cast<int>(event.type));
        return false;
    }

    return true;
}

void EventBus::StartEventLoop() {
    if (running_) {
        return;
    }

    running_ = true;

    xTaskCreate(
        [](void* arg) {
            static_cast<EventBus*>(arg)->EventLoopTask();
        },
        "event_loop",
        4096,
        this,
        5,  // 中等优先级
        &event_loop_task_
    );

    ESP_LOGI(TAG, "Event loop started");
}

void EventBus::StopEventLoop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 等待任务退出
    if (event_loop_task_) {
        // 发送一个空事件唤醒任务
        QueuedEvent dummy;
        memset(&dummy, 0, sizeof(dummy));
        dummy.type = EventType::EVENT_TYPE_MAX;
        xQueueSend(event_queue_, &dummy, 0);

        vTaskDelay(pdMS_TO_TICKS(100));
        event_loop_task_ = nullptr;
    }

    ESP_LOGI(TAG, "Event loop stopped");
}

bool EventBus::ProcessOne(int timeout_ms) {
    if (event_queue_ == nullptr) {
        return false;
    }

    QueuedEvent qe;
    TickType_t wait_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(event_queue_, &qe, wait_ticks) != pdTRUE) {
        return false;
    }

    // 跳过无效事件
    if (qe.type == EventType::EVENT_TYPE_MAX) {
        return false;
    }

    // 重建事件对象并分发
    switch (qe.type) {
        case EventType::SYSTEM_ERROR: {
            ErrorEvent e;
            e.type = qe.type;
            e.timestamp = qe.timestamp;
            e.code = qe.error_code;
            e.message = qe.message;
            Emit(e);
            break;
        }
        case EventType::DISPLAY_SET_EMOTION: {
            DisplayEvent e(qe.type);
            e.timestamp = qe.timestamp;
            e.emotion = qe.emotion;
            Emit(e);
            break;
        }
        case EventType::DISPLAY_SET_TEXT: {
            DisplayEvent e(qe.type);
            e.timestamp = qe.timestamp;
            e.text = qe.text;
            Emit(e);
            break;
        }
        default: {
            // 基本事件
            if (qe.type >= EventType::CONN_STARTING &&
                qe.type <= EventType::CONN_HEARTBEAT_TIMEOUT) {
                ConnectionEvent e(qe.type);
                e.timestamp = qe.timestamp;
                e.error_code = qe.error_code;
                e.error_message = qe.message;
                Emit(e);
            } else {
                Event e(qe.type);
                e.timestamp = qe.timestamp;
                Emit(e);
            }
            break;
        }
    }

    return true;
}

void EventBus::EventLoopTask() {
    ESP_LOGI(TAG, "Event loop task started");

    while (running_) {
        ProcessOne(100);  // 100ms 超时
    }

    ESP_LOGI(TAG, "Event loop task exiting");
    vTaskDelete(nullptr);
}

int EventBus::GetSubscriberCount(EventType type) const {
    int count = 0;

    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        auto it = subscribers_.find(type);
        if (it != subscribers_.end()) {
            count = it->second.size();
        }
        xSemaphoreGive(mutex_);
    }

    return count;
}

int EventBus::GetQueuedEventCount() const {
    if (event_queue_ == nullptr) {
        return 0;
    }
    return uxQueueMessagesWaiting(event_queue_);
}
