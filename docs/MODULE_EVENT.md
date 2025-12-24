# Event 模块分析

## 1. 模块概述

Event 模块实现了发布/订阅模式的事件系统，用于模块间解耦通信：
- **EventBus**: 事件总线，核心分发器
- **EventBridge**: 事件桥接，简化 API
- **EventTypes**: 事件类型定义

## 2. 事件类型定义

### 2.1 事件分类

```cpp
enum class EventType {
    // ========== 用户交互事件 ==========
    USER_BUTTON_PRESSED,      // Boot 按钮按下
    USER_TOUCH_HEAD,          // 触摸头部
    USER_TOUCH_CHIN,          // 触摸下巴
    USER_TOUCH_LEFT,          // 触摸左脸
    USER_TOUCH_RIGHT,         // 触摸右脸
    USER_WAKE_WORD,           // 唤醒词检测到
    USER_ABORT,               // 用户中断

    // ========== 连接状态事件 ==========
    CONN_STARTING,            // 开始连接
    CONN_SUCCESS,             // 连接成功
    CONN_FAILED,              // 连接失败
    CONN_DISCONNECTED,        // 连接断开
    CONN_RECONNECTING,        // 正在重连
    CONN_HEARTBEAT_TIMEOUT,   // 心跳超时

    // ========== 音频事件 ==========
    AUDIO_INPUT_START,        // 用户语音输入开始
    AUDIO_INPUT_END,          // 用户语音输入结束
    AUDIO_INPUT_VAD,          // VAD 状态变化
    AUDIO_OUTPUT_START,       // TTS 音频开始
    AUDIO_OUTPUT_DATA,        // TTS 音频数据
    AUDIO_OUTPUT_END,         // TTS 音频结束
    AUDIO_PLAYBACK_STARTED,   // 播放实际开始
    AUDIO_PLAYBACK_COMPLETE,  // 播放完成
    AUDIO_BUFFER_LOW,         // 缓冲区低水位

    // ========== 显示事件 ==========
    DISPLAY_SET_EMOTION,      // 设置表情
    DISPLAY_SET_TEXT,         // 设置文本
    DISPLAY_SET_STATUS,       // 设置状态栏
    DISPLAY_POWER_SAVE,       // 省电模式

    // ========== 系统事件 ==========
    SYSTEM_ERROR,             // 系统错误
    SYSTEM_IDLE_TIMEOUT,      // 空闲超时
    SYSTEM_LOW_BATTERY,       // 低电量
    SYSTEM_REBOOT,            // 重启请求
};
```

### 2.2 事件结构

```cpp
// 基类
struct Event {
    EventType type;
    uint32_t timestamp;
    Event(EventType t) : type(t), timestamp(0) {}
    virtual ~Event() = default;
};

// 用户事件
struct UserEvent : Event {
    std::string wake_word;
    std::string touch_prompt;
};

// 连接事件
struct ConnectionEvent : Event {
    int error_code = 0;
    std::string error_message;
    int retry_count = 0;
};

// 音频数据事件
struct AudioDataEvent : Event {
    std::vector<uint8_t> data;
    uint32_t sequence = 0;
    int duration_ms = 0;
};

// 显示事件
struct DisplayEvent : Event {
    std::string emotion;
    std::string text;
    std::string role;
    bool power_save = false;
};

// 错误事件
struct ErrorEvent : Event {
    int code = 0;
    std::string message;
    std::string category;
};
```

## 3. EventBus 实现

### 3.1 类结构

```cpp
class EventBus {
public:
    enum Priority { LOW = 0, NORMAL = 1, HIGH = 2 };

    static EventBus& GetInstance();

    // 订阅/取消订阅
    int Subscribe(EventType type, EventHandler handler, Priority priority = NORMAL);
    void Unsubscribe(EventType type, int handler_id);

    // 事件发送
    void Emit(const Event& event);           // 同步
    bool EmitAsync(const Event& event);      // 异步

    // 事件循环
    void StartEventLoop();
    void StopEventLoop();
    bool ProcessOne(int timeout_ms = 0);

private:
    struct Subscriber {
        int id;
        EventHandler handler;
        Priority priority;
    };

    struct QueuedEvent {
        EventType type;
        uint32_t timestamp;
        int error_code;
        char message[128];
        char emotion[32];
        char text[256];
    };

    std::map<EventType, std::vector<Subscriber>> subscribers_;
    SemaphoreHandle_t mutex_;
    QueueHandle_t event_queue_;
    TaskHandle_t event_loop_task_;
    std::atomic<int> next_id_{1};
    std::atomic<bool> running_{false};

    static const int EVENT_QUEUE_SIZE = 32;
};
```

### 3.2 订阅机制

```cpp
int EventBus::Subscribe(EventType type, EventHandler handler, Priority priority) {
    xSemaphoreTake(mutex_, portMAX_DELAY);

    int id = next_id_++;

    Subscriber sub{id, std::move(handler), priority};

    auto& subs = subscribers_[type];
    // 按优先级插入 (高优先级在前)
    auto it = std::find_if(subs.begin(), subs.end(),
        [priority](const Subscriber& s) { return s.priority < priority; });
    subs.insert(it, std::move(sub));

    xSemaphoreGive(mutex_);
    return id;
}

void EventBus::Unsubscribe(EventType type, int handler_id) {
    xSemaphoreTake(mutex_, portMAX_DELAY);

    auto& subs = subscribers_[type];
    subs.erase(
        std::remove_if(subs.begin(), subs.end(),
            [handler_id](const Subscriber& s) { return s.id == handler_id; }),
        subs.end()
    );

    xSemaphoreGive(mutex_);
}
```

### 3.3 同步发送

```cpp
void EventBus::Emit(const Event& event) {
    xSemaphoreTake(mutex_, portMAX_DELAY);

    auto it = subscribers_.find(event.type);
    if (it != subscribers_.end()) {
        // 复制订阅者列表 (避免回调中修改)
        auto handlers = it->second;
        xSemaphoreGive(mutex_);

        // 按优先级顺序执行
        for (const auto& sub : handlers) {
            try {
                sub.handler(event);
            } catch (...) {
                ESP_LOGE(TAG, "Handler exception for event %d", (int)event.type);
            }
        }
    } else {
        xSemaphoreGive(mutex_);
    }
}
```

### 3.4 异步发送

```cpp
bool EventBus::EmitAsync(const Event& event) {
    QueuedEvent qe;
    qe.type = event.type;
    qe.timestamp = event.timestamp;

    // 根据事件类型复制数据
    if (auto* e = dynamic_cast<const ErrorEvent*>(&event)) {
        qe.error_code = e->code;
        strncpy(qe.message, e->message.c_str(), sizeof(qe.message) - 1);
    } else if (auto* e = dynamic_cast<const DisplayEvent*>(&event)) {
        strncpy(qe.emotion, e->emotion.c_str(), sizeof(qe.emotion) - 1);
        strncpy(qe.text, e->text.c_str(), sizeof(qe.text) - 1);
    }

    return xQueueSend(event_queue_, &qe, 0) == pdTRUE;
}
```

### 3.5 事件循环任务

```cpp
void EventBus::EventLoopTask() {
    QueuedEvent qe;

    while (running_) {
        if (xQueueReceive(event_queue_, &qe, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 重建事件对象
            Event event(qe.type);
            event.timestamp = qe.timestamp;

            // 分发给订阅者
            Emit(event);
        }
    }
}
```

## 4. EventBridge 桥接

### 4.1 设计目的

EventBridge 提供简单的静态方法，便于现有代码逐步迁移到事件系统。

### 4.2 实现

```cpp
// event_bridge.h
class EventBridge {
public:
    static void EmitConnectionStart();
    static void EmitConnectionSuccess();
    static void EmitConnectionFailed(int error_code = 0, const char* message = "");
    static void EmitConnectionDisconnected();

    static void EmitAudioOutputStart();
    static void EmitAudioOutputData(const uint8_t* data, size_t len, int duration_ms = 60);
    static void EmitAudioOutputEnd();

    static void EmitSetEmotion(const char* emotion);
    static void EmitSetText(const char* text, const char* role = "assistant");
    static void EmitSetStatus(const char* status);

    static void EmitUserButtonPressed();
    static void EmitUserWakeWord(const char* wake_word = "");
    static void EmitUserAbort();

    static void EmitSystemError(int code, const char* message, const char* category = "system");
};
```

```cpp
// event_bridge.cc
void EventBridge::EmitConnectionStart() {
    ConnectionEvent event(EventType::CONN_STARTING);
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitSetEmotion(const char* emotion) {
    DisplayEvent event(EventType::DISPLAY_SET_EMOTION);
    event.emotion = emotion ? emotion : "";
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitAudioOutputData(const uint8_t* data, size_t len, int duration_ms) {
    AudioDataEvent event(EventType::AUDIO_OUTPUT_DATA);
    if (data && len > 0) {
        event.data.assign(data, data + len);
    }
    event.duration_ms = duration_ms;
    EventBus::GetInstance().Emit(event);
}
```

## 5. 使用示例

### 5.1 订阅事件

```cpp
void DisplayEngine::Initialize() {
    auto& bus = EventBus::GetInstance();

    // 订阅表情事件 (高优先级)
    emotion_handler_id_ = bus.Subscribe(
        EventType::DISPLAY_SET_EMOTION,
        [this](const Event& e) {
            auto& de = static_cast<const DisplayEvent&>(e);
            SetEmotion(de.emotion);
        },
        EventBus::HIGH
    );

    // 订阅文本事件
    text_handler_id_ = bus.Subscribe(
        EventType::DISPLAY_SET_TEXT,
        [this](const Event& e) {
            auto& de = static_cast<const DisplayEvent&>(e);
            SetText(de.text, de.role);
        }
    );
}

DisplayEngine::~DisplayEngine() {
    auto& bus = EventBus::GetInstance();
    bus.Unsubscribe(EventType::DISPLAY_SET_EMOTION, emotion_handler_id_);
    bus.Unsubscribe(EventType::DISPLAY_SET_TEXT, text_handler_id_);
}
```

### 5.2 发布事件

```cpp
// 使用 EventBridge (推荐)
void Application::SetDeviceState(DeviceState state) {
    switch (state) {
        case kDeviceStateListening:
            EventBridge::EmitSetEmotion("neutral");
            break;
        case kDeviceStateSpeaking:
            EventBridge::EmitSetEmotion("speaking");
            break;
    }
}

// 直接使用 EventBus
void AudioService::OnPlaybackComplete() {
    Event event(EventType::AUDIO_PLAYBACK_COMPLETE);
    EventBus::GetInstance().Emit(event);
}
```

## 6. 事件流示意

### 6.1 对话流程事件

```
┌──────────────┐     USER_BUTTON_PRESSED     ┌──────────────┐
│    Idle      │────────────────────────────▶│  Connecting  │
└──────────────┘                             └──────────────┘
                                                    │
                                             CONN_SUCCESS
                                                    ▼
┌──────────────┐     AUDIO_OUTPUT_START      ┌──────────────┐
│   Speaking   │◀────────────────────────────│  Listening   │
└──────────────┘                             └──────────────┘
       │
       │ AUDIO_PLAYBACK_COMPLETE
       ▼
┌──────────────┐
│    Idle      │
└──────────────┘
```

### 6.2 表情更新事件

```
┌───────────────┐                          ┌───────────────┐
│  Application  │──DISPLAY_SET_EMOTION────▶│ DisplayEngine │
└───────────────┘                          └───────────────┘
                                                   │
                                                   ▼
                                           ┌───────────────┐
                                           │ EmotionState  │
                                           └───────────────┘
                                                   │
                                                   ▼
                                           ┌───────────────┐
                                           │   LcdDisplay  │
                                           └───────────────┘
```

## 7. 线程安全

### 7.1 互斥保护

```cpp
// 订阅者列表访问需要加锁
xSemaphoreTake(mutex_, portMAX_DELAY);
// 操作 subscribers_
xSemaphoreGive(mutex_);
```

### 7.2 事件处理中不持锁

```cpp
void EventBus::Emit(const Event& event) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto handlers = it->second;  // 复制
    xSemaphoreGive(mutex_);      // 释放锁

    // 回调执行时不持锁，避免死锁
    for (const auto& sub : handlers) {
        sub.handler(event);
    }
}
```

## 8. 潜在问题

### 8.1 处理器异常

**问题**: 回调抛出异常导致后续处理器不执行

**解决**: try-catch 包装每个回调

### 8.2 订阅者泄漏

**问题**: 忘记取消订阅导致悬垂指针

**解决**: RAII 封装订阅生命周期

### 8.3 异步队列满

**问题**: EmitAsync 失败，事件丢失

**解决**: 返回 bool 让调用者处理

---

*文档版本: 1.0*
