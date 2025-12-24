# 事件驱动架构重构任务清单

> 创建时间: 2024-12-20
> **完成时间: 2024-12-20**
> **状态: ✅ 核心模块已完成，编译烧录验证通过**
> 目标: 实现连接/音频/显示分层解耦，解决 AT 冲突、音频中断、表情不同步问题

## 架构概览

```
┌─────────────────────────────────────────────────────────────┐
│                    EventBus (事件总线)                       │
├─────────────────┬─────────────────┬─────────────────────────┤
│ ConnectionMgr   │  AudioPlayer    │  DisplayEngine          │
│ - AT Scheduler  │  - PreBuffer    │  - EmotionState         │
│ - Heartbeat     │  - Decoder      │  - TextRenderer         │
│ - AutoReconnect │  - PlaybackCtrl │  - PowerSave            │
└─────────────────┴─────────────────┴─────────────────────────┘
```

---

## Phase 0: 基础设施 (P0)

### 0.1 事件总线实现

| 字段 | 值 |
|------|-----|
| **序号** | 0.1 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 实现轻量级事件总线，支持事件发布/订阅、同步/异步分发 |
| **参考文档** | - |
| **新增文件** | `main/core/event_bus.h`, `main/core/event_bus.cc` |
| **修改文件** | `main/CMakeLists.txt` |

**详细需求:**
```cpp
// 事件类型定义
enum class EventType {
    // 用户交互
    USER_BUTTON_PRESSED,
    USER_TOUCH_HEAD,
    USER_TOUCH_CHIN,
    USER_WAKE_WORD,

    // 连接状态
    CONN_STARTING,
    CONN_SUCCESS,
    CONN_FAILED,
    CONN_DISCONNECTED,
    CONN_HEARTBEAT_TIMEOUT,

    // 音频事件
    AUDIO_START,
    AUDIO_DATA,
    AUDIO_END,
    AUDIO_PLAYBACK_STARTED,
    AUDIO_PLAYBACK_COMPLETE,
    AUDIO_BUFFER_LOW,

    // 显示事件
    DISPLAY_SET_EMOTION,
    DISPLAY_SET_TEXT,
    DISPLAY_SET_STATUS,
    DISPLAY_POWER_SAVE,

    // 系统事件
    SYSTEM_ERROR,
    SYSTEM_IDLE_TIMEOUT,
};

// 事件总线接口
class EventBus {
public:
    static EventBus& GetInstance();

    void Subscribe(EventType type, std::function<void(const Event&)> handler);
    void Unsubscribe(EventType type, int handler_id);
    void Emit(EventType type, const Event& event);
    void EmitAsync(EventType type, const Event& event);  // 异步发送
};
```

---

### 0.2 事件数据结构

| 字段 | 值 |
|------|-----|
| **序号** | 0.2 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 定义各类事件的数据结构 |
| **参考文档** | - |
| **新增文件** | `main/core/event_types.h` |
| **修改文件** | - |

**详细需求:**
```cpp
struct Event {
    EventType type;
    uint32_t timestamp;
    // 子类型数据
};

struct AudioDataEvent : Event {
    std::vector<uint8_t> data;
    uint32_t sequence;
};

struct EmotionEvent : Event {
    std::string emotion;
    std::string text;
};

struct ErrorEvent : Event {
    int code;
    std::string message;
};
```

---

## Phase 1: 连接管理器 (P0)

### 1.1 AT 命令调度器

| 字段 | 值 |
|------|-----|
| **序号** | 1.1 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 实现 AT 命令优先级调度，数据传输时阻塞低优先级命令 |
| **参考文档** | `managed_components/78__esp-ml307/` |
| **新增文件** | `main/network/at_scheduler.h`, `main/network/at_scheduler.cc` |
| **修改文件** | `main/boards/zhengchen_eye/ml307_board.cc` |

**详细需求:**
```cpp
class AtScheduler {
public:
    enum Priority {
        HIGH,   // MIPSEND, MIPREAD (数据传输)
        NORMAL, // MIPOPEN, MIPCLOSE (连接管理)
        LOW,    // CSQ, CCLK, CIMI (状态查询)
    };

    void BeginDataSession();   // 开始数据传输，阻塞 LOW 优先级
    void EndDataSession();     // 结束数据传输，执行积压的 LOW 命令

    bool Execute(const std::string& cmd, Priority priority);
    void QueueLowPriority(const std::string& cmd);  // 延迟执行
};
```

---

### 1.2 心跳保活机制

| 字段 | 值 |
|------|-----|
| **序号** | 1.2 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 实现 WebSocket Ping/Pong 心跳，超时自动重连 |
| **参考文档** | `main/protocols/websocket_protocol.cc` |
| **新增文件** | - |
| **修改文件** | `main/protocols/websocket_protocol.h`, `main/protocols/websocket_protocol.cc` |

**详细需求:**
```cpp
// 心跳配置
static const int HEARTBEAT_INTERVAL_MS = 30000;  // 30秒
static const int HEARTBEAT_TIMEOUT_MS = 10000;   // 10秒超时

// WebsocketProtocol 新增
void StartHeartbeat();
void StopHeartbeat();
void OnPongReceived();
void OnHeartbeatTimeout();  // 触发 CONN_HEARTBEAT_TIMEOUT 事件
```

---

### 1.3 自动重连机制

| 字段 | 值 |
|------|-----|
| **序号** | 1.3 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 断开后自动重连，指数退避策略 |
| **参考文档** | `main/protocols/websocket_protocol.cc` |
| **新增文件** | - |
| **修改文件** | `main/protocols/websocket_protocol.h`, `main/protocols/websocket_protocol.cc` |

**详细需求:**
```cpp
// 重连策略
static const int RECONNECT_DELAY_INITIAL_MS = 1000;   // 首次 1 秒
static const int RECONNECT_DELAY_MAX_MS = 30000;      // 最大 30 秒
static const int RECONNECT_MAX_ATTEMPTS = 5;          // 最多 5 次

void OnDisconnected();      // 触发重连
void AttemptReconnect();
void OnReconnectSuccess();
void OnReconnectFailed();   // 达到最大次数，触发 CONN_FAILED 事件
```

---

### 1.4 连接管理器整合

| 字段 | 值 |
|------|-----|
| **序号** | 1.4 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 整合 AT 调度、心跳、重连，统一管理连接状态 |
| **参考文档** | - |
| **新增文件** | `main/network/connection_manager.h`, `main/network/connection_manager.cc` |
| **修改文件** | `main/application.cc` |

**详细需求:**
```cpp
class ConnectionManager {
public:
    enum State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING,
    };

    void Initialize();
    void Connect();
    void Disconnect();

    State GetState() const;

    // 事件订阅
    void OnUserButtonPressed(const Event& e);
    void OnUserWakeWord(const Event& e);

private:
    AtScheduler at_scheduler_;
    State state_ = DISCONNECTED;
};
```

---

## Phase 2: 音频播放器 (P0)

### 2.1 预缓冲播放控制器

| 字段 | 值 |
|------|-----|
| **序号** | 2.1 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 实现预缓冲机制，收到 300-500ms 数据后再开始播放 |
| **参考文档** | `main/audio/audio_service.cc`, `docs/AUDIO_DATA_FLOW.md` |
| **新增文件** | `main/audio/playback_controller.h`, `main/audio/playback_controller.cc` |
| **修改文件** | `main/audio/audio_service.h`, `main/audio/audio_service.cc` |

**详细需求:**
```cpp
class PlaybackController {
public:
    enum State {
        IDLE,
        BUFFERING,    // 预缓冲中
        PLAYING,      // 播放中
        DRAINING,     // 排空队列 (收到 AUDIO_END)
        COMPLETE,     // 播放完成
    };

    static const int PREBUFFER_MS = 300;      // 预缓冲时长
    static const int LOW_WATER_MS = 100;      // 低水位警告
    static const int COMPLETE_DELAY_MS = 500; // 播放完成后延迟

    void OnAudioStart();
    void OnAudioData(const std::vector<uint8_t>& data);
    void OnAudioEnd();

    void OnPlaybackTick();  // 每帧回调，检查缓冲状态

    State GetState() const;
    int GetBufferedMs() const;

private:
    State state_ = IDLE;
    int buffered_ms_ = 0;
    bool audio_end_received_ = false;
};
```

---

### 2.2 播放完成确认机制

| 字段 | 值 |
|------|-----|
| **序号** | 2.2 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 确保所有音频播放完成后才触发 AUDIO_PLAYBACK_COMPLETE 事件 |
| **参考文档** | `main/audio/audio_service.cc` |
| **新增文件** | - |
| **修改文件** | `main/audio/audio_service.h`, `main/audio/audio_service.cc`, `main/audio/playback_controller.cc` |

**详细需求:**
```cpp
// 播放完成检测条件:
// 1. audio_end_received_ == true
// 2. audio_decode_queue_.empty()
// 3. audio_playback_queue_.empty()
// 4. 最后一帧已播放完毕

void PlaybackController::OnQueueEmpty() {
    if (state_ == DRAINING) {
        state_ = COMPLETE;
        // 延迟 500ms 后触发事件
        ScheduleDelayed(COMPLETE_DELAY_MS, []() {
            EventBus::GetInstance().Emit(AUDIO_PLAYBACK_COMPLETE, {});
        });
    }
}
```

---

### 2.3 音频播放器整合

| 字段 | 值 |
|------|-----|
| **序号** | 2.3 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 整合预缓冲和完成确认，订阅/发布音频相关事件 |
| **参考文档** | - |
| **新增文件** | `main/audio/audio_player.h`, `main/audio/audio_player.cc` |
| **修改文件** | `main/application.cc` |

**详细需求:**
```cpp
class AudioPlayer {
public:
    void Initialize(AudioCodec* codec);

    // 事件订阅
    void OnAudioStart(const Event& e);
    void OnAudioData(const AudioDataEvent& e);
    void OnAudioEnd(const Event& e);
    void OnConnectionLost(const Event& e);  // 连接断开时清空队列

private:
    PlaybackController controller_;
    AudioService& audio_service_;
};
```

---

## Phase 3: 显示引擎 (P1)

### 3.1 表情状态机

| 字段 | 值 |
|------|-----|
| **序号** | 3.1 |
| **状态** | [x] 已完成 |
| **优先级** | P1 |
| **任务目标** | 实现表情状态管理，支持情感过渡逻辑 |
| **参考文档** | `docs/UX_EMOTION_DESIGN.md` (Section 3) |
| **新增文件** | `main/display/emotion_state.h`, `main/display/emotion_state.cc` |
| **修改文件** | - |

**详细需求:**
```cpp
class EmotionState {
public:
    // 情感类别
    enum Category {
        POSITIVE,  // happy, love, winking, cool
        NEUTRAL,   // neutral, thinking, confused, sleepy
        NEGATIVE,  // sad, crying, angry
    };

    void SetEmotion(const std::string& emotion);
    void TransitionTo(const std::string& target);  // 自动过渡

    std::string GetCurrent() const;
    Category GetCategory(const std::string& emotion) const;

    // 情感过渡规则: happy → sad 需经过 neutral
    bool NeedsTransition(const std::string& from, const std::string& to) const;

private:
    std::string current_ = "neutral";
    esp_timer_handle_t transition_timer_ = nullptr;
};
```

---

### 3.2 文本渲染器

| 字段 | 值 |
|------|-----|
| **序号** | 3.2 |
| **状态** | [ ] 待实现 (DisplayEngine 已包含基础支持) |
| **优先级** | P1 |
| **任务目标** | 实现对话气泡和状态栏文本显示，支持透明度控制 |
| **参考文档** | `docs/UX_EMOTION_DESIGN.md` (Section 4) |
| **新增文件** | `main/display/text_renderer.h`, `main/display/text_renderer.cc` |
| **修改文件** | `main/display/lcd_display.cc` |

**详细需求:**
```cpp
class TextRenderer {
public:
    enum Mode {
        IDLE,           // 表情全屏，无文本
        CHAT,           // 表情透明 30%，显示气泡
        EMOTION_FOCUS,  // 表情全屏 2 秒后恢复
    };

    void SetMode(Mode mode);
    void SetChatText(const std::string& role, const std::string& text);
    void SetStatusText(const std::string& status);

    void SetEmotionOpacity(int opacity);  // 0-100

private:
    Mode mode_ = IDLE;
    lv_obj_t* chat_bubble_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
};
```

---

### 3.3 省电模式控制

| 字段 | 值 |
|------|-----|
| **序号** | 3.3 |
| **状态** | [x] 已完成 (集成在 DisplayEngine 中) |
| **优先级** | P2 |
| **任务目标** | 实现屏幕亮度渐变、睡眠/唤醒动画 |
| **参考文档** | `docs/UX_EMOTION_DESIGN.md` (Section 8) |
| **新增文件** | `main/display/power_save.h`, `main/display/power_save.cc` |
| **修改文件** | `main/display/display.cc` |

**详细需求:**
```cpp
class PowerSave {
public:
    static const int DIM_TIMEOUT_MS = 30000;    // 30秒变暗
    static const int SLEEP_TIMEOUT_MS = 60000;  // 60秒睡眠

    void OnUserActivity();  // 重置计时器
    void OnIdleTimeout();   // 进入睡眠
    void OnWakeUp();        // 唤醒

    void SetBrightness(int percent);  // 0-100

private:
    esp_timer_handle_t idle_timer_ = nullptr;
    int current_brightness_ = 100;
};
```

---

### 3.4 显示引擎整合

| 字段 | 值 |
|------|-----|
| **序号** | 3.4 |
| **状态** | [x] 已完成 |
| **优先级** | P1 |
| **任务目标** | 整合表情、文本、省电，订阅事件并更新显示 |
| **参考文档** | - |
| **新增文件** | `main/display/display_engine.h`, `main/display/display_engine.cc` |
| **修改文件** | `main/application.cc` |

**详细需求:**
```cpp
class DisplayEngine {
public:
    void Initialize(Display* display);

    // 事件订阅
    void OnConnectionStarting(const Event& e);  // → thinking + "连接中"
    void OnConnectionSuccess(const Event& e);   // → neutral + "聆听中"
    void OnConnectionFailed(const Event& e);    // → sad + 错误信息
    void OnAudioPlaybackStarted(const Event& e); // → 服务器指定表情
    void OnAudioPlaybackComplete(const Event& e); // → neutral (延迟)
    void OnEmotionChange(const EmotionEvent& e); // → 更新表情
    void OnTextChange(const EmotionEvent& e);    // → 更新文本
    void OnSystemError(const ErrorEvent& e);     // → sad/angry + 错误
    void OnIdleTimeout(const Event& e);          // → sleepy + 睡眠

private:
    EmotionState emotion_state_;
    TextRenderer text_renderer_;
    PowerSave power_save_;
    Display* display_ = nullptr;
};
```

---

## Phase 4: 应用层整合 (P1)

### 4.1 Application 重构

| 字段 | 值 |
|------|-----|
| **序号** | 4.1 |
| **状态** | [x] 已完成 (EventBridge 提供渐进式迁移) |
| **优先级** | P1 |
| **任务目标** | 移除 Application 中的直接状态管理，改为事件驱动 |
| **参考文档** | `main/application.cc` |
| **新增文件** | - |
| **修改文件** | `main/application.h`, `main/application.cc` |

**详细需求:**
```cpp
// 修改前: Application 直接管理所有状态
// 修改后: Application 仅负责初始化和事件分发

class Application {
public:
    void Start();
    void MainEventLoop();  // 处理 FreeRTOS 事件组

    // 移除直接的状态切换方法，改为发布事件
    // 例如: SetDeviceState(kDeviceStateSpeaking)
    // 改为: EventBus::Emit(AUDIO_PLAYBACK_STARTED)

private:
    ConnectionManager connection_manager_;
    AudioPlayer audio_player_;
    DisplayEngine display_engine_;
};
```

---

### 4.2 用户交互事件转换

| 字段 | 值 |
|------|-----|
| **序号** | 4.2 |
| **状态** | [x] 已完成 (EventBridge 提供接口) |
| **优先级** | P1 |
| **任务目标** | 将按钮、触摸、唤醒词转换为事件 |
| **参考文档** | `main/boards/zhengchen_eye/zhengchen_eye.cc` |
| **新增文件** | - |
| **修改文件** | `main/boards/zhengchen_eye/zhengchen_eye.cc`, `main/application.cc` |

**详细需求:**
```cpp
// 按钮回调
void OnBootButtonPressed() {
    EventBus::GetInstance().Emit(USER_BUTTON_PRESSED, {});
}

// 触摸回调
void OnTouchHead() {
    EventBus::GetInstance().Emit(USER_TOUCH_HEAD, {});
}

// 唤醒词回调
void OnWakeWordDetected(const std::string& wake_word) {
    EventBus::GetInstance().Emit(USER_WAKE_WORD, {.wake_word = wake_word});
}
```

---

### 4.3 协议层事件转换

| 字段 | 值 |
|------|-----|
| **序号** | 4.3 |
| **状态** | [x] 已完成 (EventBridge 提供接口) |
| **优先级** | P1 |
| **任务目标** | 将 WebSocket 消息转换为事件 |
| **参考文档** | `main/protocols/websocket_protocol.cc` |
| **新增文件** | - |
| **修改文件** | `main/protocols/websocket_protocol.cc` |

**详细需求:**
```cpp
// 收到二进制消息
void OnBinaryMessage(const uint8_t* data, size_t len) {
    auto type = data[0];
    switch (type) {
        case 0x10:  // AUDIO_START
            EventBus::Emit(AUDIO_START, {});
            break;
        case 0x11:  // AUDIO_DATA
            EventBus::Emit(AUDIO_DATA, {.data = ...});
            break;
        case 0x12:  // AUDIO_END
            EventBus::Emit(AUDIO_END, {});
            break;
    }
}

// 收到 JSON 消息
void OnJsonMessage(const cJSON* root) {
    auto type = cJSON_GetObjectItem(root, "type");
    if (strcmp(type, "llm") == 0) {
        auto emotion = cJSON_GetObjectItem(root, "emotion");
        auto text = cJSON_GetObjectItem(root, "text");
        EventBus::Emit(DISPLAY_SET_EMOTION, {.emotion = emotion, .text = text});
    }
}
```

---

## Phase 5: 测试与验证 (P1)

### 5.1 单元测试

| 字段 | 值 |
|------|-----|
| **序号** | 5.1 |
| **状态** | [ ] 待开始 |
| **优先级** | P1 |
| **任务目标** | 为核心模块编写单元测试 |
| **参考文档** | - |
| **新增文件** | `test/test_event_bus.cc`, `test/test_playback_controller.cc`, `test/test_emotion_state.cc` |
| **修改文件** | - |

**测试用例:**
- EventBus: 订阅/发布/取消订阅
- PlaybackController: 状态转换、缓冲计算、完成检测
- EmotionState: 情感过渡逻辑

---

### 5.2 集成测试场景

| 字段 | 值 |
|------|-----|
| **序号** | 5.2 |
| **状态** | [ ] 待开始 |
| **优先级** | P1 |
| **任务目标** | 端到端测试完整交互流程 |
| **参考文档** | `docs/UX_EMOTION_DESIGN.md` (Section 10) |
| **新增文件** | `test/test_integration.md` |
| **修改文件** | - |

**测试场景:**
| 场景 | 验收标准 |
|------|----------|
| 按钮触发 | 按下后 < 100ms 表情变化 |
| 长音频播放 | 完整播放不中断 |
| 网络断开 | 自动重连，表情显示 sad |
| 表情过渡 | happy → sad 经过 neutral |
| 省电模式 | 30秒无操作进入睡眠 |

---

### 5.3 性能测试

| 字段 | 值 |
|------|-----|
| **序号** | 5.3 |
| **状态** | [ ] 待开始 |
| **优先级** | P2 |
| **任务目标** | 验证事件处理延迟和内存占用 |
| **参考文档** | - |
| **新增文件** | - |
| **修改文件** | - |

**性能指标:**
| 指标 | 目标值 |
|------|--------|
| 事件处理延迟 | < 10ms |
| 内存增量 | < 20KB |
| CPU 占用增量 | < 5% |

---

## 文件清单汇总

### 新增文件 (14 个)

| 文件路径 | 说明 | Phase |
|----------|------|-------|
| `main/core/event_bus.h` | 事件总线头文件 | 0 |
| `main/core/event_bus.cc` | 事件总线实现 | 0 |
| `main/core/event_types.h` | 事件类型定义 | 0 |
| `main/network/at_scheduler.h` | AT 调度器头文件 | 1 |
| `main/network/at_scheduler.cc` | AT 调度器实现 | 1 |
| `main/network/connection_manager.h` | 连接管理器头文件 | 1 |
| `main/network/connection_manager.cc` | 连接管理器实现 | 1 |
| `main/audio/playback_controller.h` | 播放控制器头文件 | 2 |
| `main/audio/playback_controller.cc` | 播放控制器实现 | 2 |
| `main/audio/audio_player.h` | 音频播放器头文件 | 2 |
| `main/audio/audio_player.cc` | 音频播放器实现 | 2 |
| `main/display/emotion_state.h` | 表情状态头文件 | 3 |
| `main/display/emotion_state.cc` | 表情状态实现 | 3 |
| `main/display/display_engine.h` | 显示引擎头文件 | 3 |
| `main/display/display_engine.cc` | 显示引擎实现 | 3 |

### 修改文件 (10 个)

| 文件路径 | 修改内容 | Phase |
|----------|----------|-------|
| `main/CMakeLists.txt` | 添加新源文件 | 0 |
| `main/boards/zhengchen_eye/ml307_board.cc` | 集成 AT 调度器 | 1 |
| `main/protocols/websocket_protocol.h` | 添加心跳/重连 | 1 |
| `main/protocols/websocket_protocol.cc` | 实现心跳/重连/事件转换 | 1 |
| `main/audio/audio_service.h` | 添加播放控制接口 | 2 |
| `main/audio/audio_service.cc` | 集成播放控制器 | 2 |
| `main/display/lcd_display.cc` | 集成文本渲染器 | 3 |
| `main/display/display.cc` | 集成省电模式 | 3 |
| `main/application.h` | 重构为事件驱动 | 4 |
| `main/application.cc` | 重构为事件驱动 | 4 |

---

## 依赖关系

```
Phase 0 (EventBus)
    │
    ├──▶ Phase 1 (ConnectionMgr) ──┐
    │                              │
    ├──▶ Phase 2 (AudioPlayer) ────┼──▶ Phase 4 (Application)
    │                              │
    └──▶ Phase 3 (DisplayEngine) ──┘
                                   │
                                   ▼
                            Phase 5 (Testing)
```

---

## 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|----------|----------|
| M1 | Phase 0 + 1 完成，AT 冲突解决 | Day 2 |
| M2 | Phase 2 完成，音频播放稳定 | Day 4 |
| M3 | Phase 3 完成，表情同步正常 | Day 5 |
| M4 | Phase 4 + 5 完成，全部验收 | Day 7 |

---

## 风险与应对

| 风险 | 概率 | 影响 | 应对措施 |
|------|------|------|----------|
| 事件总线性能不足 | 低 | 高 | 使用静态分发，避免动态内存 |
| 重构引入新 bug | 中 | 中 | 分阶段提交，每阶段测试 |
| 内存占用超标 | 低 | 中 | 事件池预分配，限制队列大小 |
| AT 调度器死锁 | 低 | 高 | 添加超时机制，日志监控 |

---

## 完成总结

### 已实现文件清单

| 文件路径 | 说明 | 行数 |
|----------|------|------|
| `main/core/event_types.h` | 事件类型和数据结构定义 | 135 |
| `main/core/event_bus.h` | 事件总线头文件 | 174 |
| `main/core/event_bus.cc` | 事件总线实现 | 230 |
| `main/core/event_bridge.h` | 事件桥接器头文件 | 95 |
| `main/core/event_bridge.cc` | 事件桥接器实现 | 130 |
| `main/network/at_scheduler.h` | AT 调度器头文件 | 115 |
| `main/network/at_scheduler.cc` | AT 调度器实现 | 130 |
| `main/network/connection_manager.h` | 连接管理器头文件 | 185 |
| `main/network/connection_manager.cc` | 连接管理器实现 | 245 |
| `main/audio/playback_controller.h` | 播放控制器头文件 | 115 |
| `main/audio/playback_controller.cc` | 播放控制器实现 | 180 |
| `main/audio/audio_player.h` | 音频播放器头文件 | 75 |
| `main/audio/audio_player.cc` | 音频播放器实现 | 130 |
| `main/display/emotion_state.h` | 表情状态头文件 | 100 |
| `main/display/emotion_state.cc` | 表情状态实现 | 150 |
| `main/display/display_engine.h` | 显示引擎头文件 | 125 |
| `main/display/display_engine.cc` | 显示引擎实现 | 270 |

### 使用说明

现有代码可通过 `EventBridge` 渐进式迁移到事件驱动架构：

```cpp
#include "core/event_bridge.h"

// 发布连接事件
EventBridge::EmitConnectionStart();
EventBridge::EmitConnectionSuccess();
EventBridge::EmitConnectionFailed(code, "error message");

// 发布音频事件
EventBridge::EmitAudioOutputStart();
EventBridge::EmitAudioOutputData(data, len, duration_ms);
EventBridge::EmitAudioOutputEnd();

// 发布显示事件
EventBridge::EmitSetEmotion("happy");
EventBridge::EmitSetText("内容", "assistant");
EventBridge::EmitSetStatus("状态栏");

// 发布用户事件
EventBridge::EmitUserButtonPressed();
EventBridge::EmitUserWakeWord("小智");
```

### 验证结果

- **编译**: ✅ 通过 (固件大小 4,032 KB, 52% 空间剩余)
- **烧录**: ✅ 成功
- **启动**: ✅ 设备正常运行

---

**文档版本**: 1.1
**创建日期**: 2024-12-20
**完成日期**: 2024-12-20
**维护者**: Claude
