# 设备交互逻辑分析

> 创建时间: 2024-12-20
> 针对板子: zhengchen_eye

## 1. 触发方式

zhengchen_eye 设备支持多种交互触发方式，**不强制需要唤醒词**：

| 触发方式 | 代码位置 | 说明 |
|----------|----------|------|
| **Boot 按钮** | `boot_button_.OnClick()` → `app.ToggleChatState()` | 物理按钮直接切换对话状态 |
| **触摸传感器** | `touch_sensor_` → `app.WakeWordInvoke("...")` | 触摸时带自定义提示语 |
| **唤醒词** | 可选配置 (`CONFIG_USE_*_WAKE_WORD`) | 语音唤醒（可选） |

### 触摸传感器配置

```cpp
// zhengchen_eye.cc
touch_sensor_.OnTouchEvent([](TouchEvent event) {
    switch (event) {
        case kTouchEventHead:
            app.WakeWordInvoke("(正在抚摸你的头，请提供相关的情绪价值，回答)");
            break;
        case kTouchEventChin:
            app.WakeWordInvoke("(正在抚摸你的下巴，请提供相关的情绪价值，回答)");
            break;
        // ...
    }
});
```

### 唤醒词配置选项

唤醒词检测是**可选的**，通过编译配置控制：
- `CONFIG_USE_ESP_WAKE_WORD` - ESP 原生唤醒词
- `CONFIG_USE_AFE_WAKE_WORD` - AFE 唤醒词
- `CONFIG_USE_CUSTOM_WAKE_WORD` - 自定义唤醒词

## 2. 音频录音与传输流程

```
用户操作 (按钮/触摸/唤醒词)
    ↓
ToggleChatState() / WakeWordInvoke()
    ↓
SetDeviceState(kDeviceStateConnecting)
    ↓
protocol_->OpenAudioChannel()  // WebSocket 连接
    ↓
SetDeviceState(kDeviceStateListening)
    ↓
StartListening() - 开始录音
    ↓
audio_start 事件 → WebSocket 发送开始帧
    ↓
音频采样 (16kHz) → Opus编码 (60ms帧)
    ↓
audio_data 事件 → WebSocket 二进制发送
    ↓
StopListening() - 停止录音
    ↓
audio_end 事件 → WebSocket 发送结束帧
    ↓
服务器处理: ASR → LLM → TTS
    ↓
接收 TTS 音频 → 播放
    ↓
SetDeviceState(kDeviceStateIdle)
```

### 音频格式

| 参数 | 值 |
|------|-----|
| 采样率 | 16kHz |
| 编码 | Opus |
| 帧长度 | 60ms |
| 协议 | WebSocket Binary (BinaryProtocol3) |

## 3. 屏幕状态变化

### 设备状态与屏幕显示对应关系

| 设备状态 | 状态栏文字 | 表情动画 | 触发条件 |
|----------|------------|----------|----------|
| `kDeviceStateIdle` | "待机" | neutral | 初始状态/对话结束 |
| `kDeviceStateConnecting` | "连接中" | neutral | 按下按钮/触摸 |
| `kDeviceStateListening` | "聆听中" | neutral | WebSocket 连接成功 |
| `kDeviceStateSpeaking` | "说话中" | 服务器指定 | 收到 TTS 音频 |

### 代码实现 (application.cc)

```cpp
void Application::SetDeviceState(DeviceState state) {
    switch (state) {
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);  // "待机"
            display->SetEmotion("neutral");
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);  // "连接中"
            display->SetEmotion("neutral");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);  // "聆听中"
            display->SetEmotion("neutral");
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);  // "说话中"
            // emotion 由服务器通过 WebSocket 消息控制
            break;
    }
}
```

## 4. 表情动画系统

设备支持丰富的 GIF 表情动画，定义在 `lcd_display.cc`：

### 支持的表情列表

| 表情名称 | GIF 动画 | 使用场景 |
|----------|----------|----------|
| neutral | 默认表情 | 待机、聆听 |
| happy | 开心 | 积极回复 |
| laughing | 大笑 | 幽默场景 |
| funny | 有趣 | 轻松对话 |
| sad | 伤心 | 消极情感 |
| crying | 哭泣 | 悲伤场景 |
| angry | 生气 | 负面情感 |
| love/loving | 爱心 | 表达喜爱 |
| kissy | 亲吻 | 亲密表达 |
| thinking | 思考 | 正在处理 |
| confused | 困惑 | 不理解 |
| embarrassed | 尴尬 | 社交场景 |
| surprised | 惊讶 | 意外情况 |
| shocked | 震惊 | 强烈惊讶 |
| sleepy | 困倦 | 疲惫状态 |
| winking | 眨眼 | 俏皮 |
| cool | 酷 | 自信 |
| relaxed | 放松 | 轻松 |
| delicious | 美味 | 享受 |
| confident | 自信 | 确定 |
| silly | 傻傻的 | 搞笑 |

### 服务器控制表情

服务器通过 WebSocket JSON 消息控制设备表情：

```json
{
    "type": "llm",
    "emotion": "happy"
}
```

设备收到后调用：
```cpp
display->SetEmotion(emotion_str.c_str());
```

## 5. 完整交互时序

```
┌─────────────────────────────────────────────────────────────────┐
│ 用户                  设备                      服务器          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  按下按钮 ────────►  屏幕: "连接中" + neutral                   │
│                      │                                          │
│                      ├──── WebSocket Connect ────►              │
│                      │                                          │
│                      │◄─── Connection OK ────────               │
│                      │                                          │
│                      屏幕: "聆听中" + neutral                   │
│                      │                                          │
│  开始说话            ├──── audio_start ──────────►              │
│                      ├──── audio_data (Opus) ────►              │
│                      ├──── audio_data (Opus) ────►              │
│  停止说话            ├──── audio_end ────────────►              │
│                      │                                          │
│                      │◄─── {type:"llm", emotion:"thinking"} ─── │
│                      屏幕: "说话中" + thinking                  │
│                      │                                          │
│                      │◄─── TTS audio ────────────               │
│                      播放音频                                    │
│                      │                                          │
│                      │◄─── {type:"llm", emotion:"happy"} ────── │
│                      屏幕: "说话中" + happy                     │
│                      │                                          │
│                      │◄─── TTS complete ─────────               │
│                      │                                          │
│                      屏幕: "待机" + neutral                     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 6. 相关文件

| 文件 | 说明 |
|------|------|
| `main/application.cc` | 主应用逻辑，状态机管理 |
| `main/application.h` | 状态定义，事件定义 |
| `main/display/lcd_display.cc` | LCD 显示实现，表情动画 |
| `main/display/display.h` | 显示接口定义 |
| `main/boards/zhengchen_eye/zhengchen_eye.cc` | 板级实现，按钮/触摸配置 |
| `main/protocols/websocket_protocol.cc` | WebSocket 协议实现 |

## 7. 待讨论问题

1. 触摸传感器的自定义提示语是否需要调整？
2. 表情动画的切换时机是否合理？
3. 是否需要添加更多的屏幕状态反馈？
4. 唤醒词功能是否需要启用？
