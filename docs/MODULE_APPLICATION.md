# Application 模块分析

## 1. 模块概述

Application 是系统的核心控制器，采用单例模式，负责：
- 系统初始化
- 设备状态机管理
- 主事件循环
- 任务调度

## 2. 类结构

```cpp
class Application {
public:
    // 单例访问
    static Application& GetInstance();

    // 生命周期
    void Start();              // 系统启动
    void MainEventLoop();      // 主事件循环
    void Reboot();             // 系统重启

    // 状态管理
    DeviceState GetDeviceState() const;
    void SetDeviceState(DeviceState state);

    // 用户交互
    void ToggleChatState();           // 切换对话状态
    void StartListening();            // 开始监听
    void StopListening();             // 停止监听
    void WakeWordInvoke(const std::string&);  // 唤醒词触发
    void AbortSpeaking(AbortReason);  // 中断播放

    // 任务调度
    void Schedule(std::function<void()> callback);

    // 辅助功能
    void Alert(const char* status, const char* message, ...);
    void DismissAlert();
    void PlaySound(const std::string_view& sound);

private:
    // 核心组件
    std::unique_ptr<Protocol> protocol_;
    AudioService audio_service_;
    DisplayEngine display_engine_;

    // 状态
    volatile DeviceState device_state_;
    ListeningMode listening_mode_;
    AecMode aec_mode_;

    // 任务队列
    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    EventGroupHandle_t event_group_;
};
```

## 3. 事件组定义

```cpp
#define MAIN_EVENT_SCHEDULE             (1 << 0)  // 有调度任务
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)  // 发送音频
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)  // 唤醒词检测
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)  // VAD 状态变化
#define MAIN_EVENT_ERROR                (1 << 4)  // 错误发生
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5) // OTA 检查完成
#define MAIN_EVENT_PLAYBACK_IDLE        (1 << 6)  // 播放完成
```

## 4. 状态机详解

### 4.1 状态定义

```cpp
enum DeviceState {
    kDeviceStateUnknown,       // 未知 (初始)
    kDeviceStateStarting,      // 启动中
    kDeviceStateWifiConfiguring,  // WiFi 配网
    kDeviceStateIdle,          // 待机
    kDeviceStateConnecting,    // 连接中
    kDeviceStateListening,     // 监听中
    kDeviceStateSpeaking,      // 播放中
    kDeviceStateUpgrading,     // 升级中
    kDeviceStateActivating,    // 激活中
    kDeviceStateAudioTesting,  // 音频测试
    kDeviceStateFatalError     // 致命错误
};
```

### 4.2 状态转换表

| 当前状态 | 事件/条件 | 目标状态 | 动作 |
|----------|-----------|----------|------|
| Unknown | Start() | Starting | 初始化 |
| Starting | 初始化完成 | Idle | 启用唤醒词 |
| Starting | 需要配网 | WifiConfiguring | 启动配网 |
| Idle | 按钮/唤醒词 | Connecting | 建立连接 |
| Connecting | 连接成功 | Listening | 发送 listen |
| Connecting | 连接失败 | Idle | 显示错误 |
| Listening | AUDIO_START | Speaking | 开始播放 |
| Listening | 连接断开 | Idle | 清理资源 |
| Speaking | AUDIO_END | Listening/Idle | 等待/断开 |
| Speaking | 用户中断 | Listening | 发送 abort |
| * | 致命错误 | FatalError | 显示错误 |

### 4.3 状态切换实现

```cpp
void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) return;

    DeviceState old_state = device_state_;
    device_state_ = state;

    auto* display = Board::GetInstance().GetDisplay();

    switch (state) {
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            EventBridge::EmitSetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;

        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            EventBridge::EmitSetEmotion("thinking");
            break;

        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            EventBridge::EmitSetEmotion("neutral");
            // 始终发送 listen 消息
            protocol_->SendStartListening(listening_mode_);
            if (!audio_service_.IsAudioProcessorRunning()) {
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;

        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);
            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
            }
            audio_service_.ResetDecoder();
            break;
        // ...
    }
}
```

## 5. 主事件循环

### 5.1 循环结构

```cpp
void Application::MainEventLoop() {
    while (true) {
        // 1. 等待事件 (超时 100ms)
        EventBits_t bits = xEventGroupWaitBits(
            event_group_,
            0x7F,  // 所有事件掩码
            pdTRUE,   // 清除事件位
            pdFALSE,  // 任意事件唤醒
            pdMS_TO_TICKS(100)
        );

        // 2. 处理调度任务
        if (bits & MAIN_EVENT_SCHEDULE) {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                while (!main_tasks_.empty()) {
                    task = std::move(main_tasks_.front());
                    main_tasks_.pop_front();
                    // 释放锁后执行任务
                    mutex_.unlock();
                    task();
                    mutex_.lock();
                }
            }
        }

        // 3. 处理唤醒词事件
        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        // 4. 处理发送音频事件
        if (bits & MAIN_EVENT_SEND_AUDIO) {
            // 从发送队列取音频包并发送
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                protocol_->SendAudio(std::move(packet));
            }
        }

        // 5. 处理 VAD 变化
        if (bits & MAIN_EVENT_VAD_CHANGE) {
            // 处理语音活动检测结果
        }

        // 6. 处理播放完成
        if (bits & MAIN_EVENT_PLAYBACK_IDLE) {
            if (waiting_for_playback_complete_) {
                waiting_for_playback_complete_ = false;
                // 切换状态
            }
        }

        // 7. 定期任务 (每 100ms)
        OnClockTimer();
    }
}
```

### 5.2 任务调度模式

```cpp
// 线程安全的任务投递
void Application::Schedule(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// 使用示例
void SomeOtherTask() {
    // 从其他任务调度到主循环执行
    Application::GetInstance().Schedule([this]() {
        // 在主循环上下文中执行
        SetDeviceState(kDeviceStateListening);
    });
}
```

## 6. 对话流程

### 6.1 唤醒词触发

```cpp
void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        // 1. 切换对话状态
        ToggleChatState();
        // 2. 发送唤醒词
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word);
            }
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        // 中断播放
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    }
}
```

### 6.2 按钮触发

```cpp
void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateIdle) {
        // 开始对话
        SetDeviceState(kDeviceStateConnecting);
        Schedule([this]() {
            if (protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateListening);
            } else {
                SetDeviceState(kDeviceStateIdle);
            }
        });
    } else if (device_state_ == kDeviceStateListening) {
        // 停止监听
        StopListening();
    } else if (device_state_ == kDeviceStateSpeaking) {
        // 中断播放
        AbortSpeaking(kAbortReasonNone);
    }
}
```

## 7. 协议回调注册

```cpp
void Application::Start() {
    // 初始化协议
    protocol_ = CreateProtocol();

    // 注册音频回调
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        // 接收状态检查：允许 Listening 或 Speaking
        if (device_state_ == kDeviceStateListening ||
            device_state_ == kDeviceStateSpeaking) {
            // 首次收到音频时切换状态
            if (device_state_ == kDeviceStateListening) {
                Schedule([this]() {
                    SetDeviceState(kDeviceStateSpeaking);
                });
            }
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });

    // 注册 JSON 回调
    protocol_->OnIncomingJson([this](const cJSON* root) {
        HandleIncomingJson(root);
    });

    // 注册连接回调
    protocol_->OnAudioChannelOpened([this]() {
        // 连接成功
    });

    protocol_->OnAudioChannelClosed([this]() {
        Schedule([this]() {
            SetDeviceState(kDeviceStateIdle);
        });
    });
}
```

## 8. 潜在问题

### 8.1 状态竞态

**问题**: `SetDeviceState` 在 `Schedule` 异步执行时，状态可能不一致

**现象**: 服务器发送音频时设备仍在 Listening 状态

**修复**: 在 `OnIncomingAudio` 中同时接受 `Listening` 和 `Speaking` 状态

### 8.2 资源泄漏

**问题**: `protocol_` 在异常退出时可能未正确释放

**修复**: 使用 `unique_ptr` 自动管理

### 8.3 任务队列溢出

**问题**: 大量任务调度可能导致队列过长

**修复**: 设置队列上限或合并相同任务

---

*文档版本: 1.0*
