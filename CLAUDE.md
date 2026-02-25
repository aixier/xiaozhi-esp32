# zhengchen-eye 项目 Claude Code 指南

## 快速导航

> **先读这里**: 根据你的问题类型，直接跳转到对应文档

| 我想了解... | 去哪里 |
|-------------|--------|
| 🏗️ **整体架构** | [docs/ARCHITECTURE_ANALYSIS.md](docs/ARCHITECTURE_ANALYSIS.md) |
| 🔄 **状态机/主循环** | [docs/MODULE_APPLICATION.md](docs/MODULE_APPLICATION.md) |
| 🎵 **音频采集/播放** | [docs/MODULE_AUDIO.md](docs/MODULE_AUDIO.md) |
| 📡 **WebSocket/协议** | [docs/MODULE_PROTOCOL.md](docs/MODULE_PROTOCOL.md) |
| 📢 **事件系统** | [docs/MODULE_EVENT.md](docs/MODULE_EVENT.md) |
| ⏱️ **时序图** | [docs/TIMING_DIAGRAMS.md](docs/TIMING_DIAGRAMS.md) |
| 🎭 **表情动画** | [docs/UX_EMOTION_DESIGN.md](docs/UX_EMOTION_DESIGN.md) |
| 🖥️ **显示问题** | [docs/DISPLAY_ISSUES.md](docs/DISPLAY_ISSUES.md) |
| 🔌 **API 规范** | [docs/API_SPECIFICATION.md](docs/API_SPECIFICATION.md) |
| 🚀 **私有服务器部署** | [docs/PRIVATE_SERVER_DEPLOYMENT.md](docs/PRIVATE_SERVER_DEPLOYMENT.md) |
| 📋 **4G音频改进任务** | [docs/TODO_AUDIO_CONTROL_DATA_SPLIT.md](docs/TODO_AUDIO_CONTROL_DATA_SPLIT.md) |

---

## 项目概述

ESP32-S3 语音助手固件，支持 4G (ML307) / WiFi 双网络。

**技术栈**: ESP-IDF v5.4.1 | C++ | FreeRTOS | LVGL | Opus | WebSocket

**架构分层**:
```
Application → Core(EventBus) → Services(Audio/Protocol/Display) → HAL → ESP-IDF
```

---

## 编译烧录

### Docker 编译 (推荐)
```bash
docker run --rm -v /mnt/d/work/langmem/eye/zhengchen-eye:/project -w /project espressif/idf:v5.4.1 idf.py build
```

### 烧录固件
```bash
python3 -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/srmodels/srmodels.bin \
  0x410000 build/xiaozhi.bin
```

### 查看串口日志
```bash
timeout 30 cat /dev/ttyACM0 2>&1 | head -200
```

---

## 代码导航

### 目录结构
```
main/
├── application.cc/h     # 🎯 核心入口，状态机
├── core/                # 事件系统
│   ├── event_bus.h      #   发布/订阅
│   └── event_bridge.h   #   简化 API
├── audio/               # 音频子系统
│   ├── audio_service.h  #   🎯 音频核心
│   └── playback_controller.h  # 播放控制
├── protocols/           # 通信协议
│   ├── protocol.h       #   抽象接口
│   └── websocket_protocol.cc  # 🎯 WebSocket 实现
├── display/             # 显示子系统
│   ├── display_engine.h #   显示引擎
│   └── emotion_state.h  #   表情状态
├── network/             # 网络管理
│   ├── connection_manager.h  # 连接/重连
│   └── at_scheduler.h   #   AT 命令调度
└── boards/              # 板级支持
    └── zhengchen_eye/   #   目标板
```

### 关键文件速查

| 功能 | 文件 | 核心类/函数 |
|------|------|-------------|
| 状态机 | `application.cc` | `SetDeviceState()`, `MainEventLoop()` |
| 音频编解码 | `audio_service.cc` | `OpusCodecTask()`, 队列管理 |
| WebSocket | `websocket_protocol.cc` | `OpenAudioChannel()`, `OnData()` |
| 4G 模块 | `managed_components/78__esp-ml307/` | `WebSocket`, `Tcp` |
| 表情动画 | `display/emotion_state.cc` | `SetEmotion()` |
| 事件发布 | `core/event_bridge.cc` | `EmitSetEmotion()`, `EmitAudioOutputStart()` |

---

## 核心概念

### 设备状态机
```
Idle → Connecting → Listening ⇄ Speaking → Idle
  ↑                                         │
  └─────────────────────────────────────────┘
```

### 音频数据流
```
发送: MIC → AudioProcessor → EncodeQueue → OpusEncoder → SendQueue → WebSocket
接收: WebSocket → DecodeQueue(预缓冲) → OpusDecoder → PlaybackQueue → Speaker
```

### BinaryProtocol3 消息
| 类型 | 值 | 方向 | 说明 |
|------|-----|------|------|
| AUDIO_DATA | 0x00 | 双向 | 音频数据 |
| AUDIO_START | 0x10 | 接收 | TTS 开始 |
| AUDIO_END | 0x12 | 接收 | TTS 结束 |
| TEXT_ASR | 0x20 | 接收 | ASR 识别结果 |
| TEXT_LLM | 0x21 | 接收 | LLM 响应 |

---

## 常见问题排查

### 问题路由表

| 现象 | 可能原因 | 排查文档 |
|------|----------|----------|
| 设备无声音 | TTS 配置/Opus 解析 | 见下方 TTS 排查 |
| 连接断开 | PING/PONG/心跳 | [MODULE_PROTOCOL.md](docs/MODULE_PROTOCOL.md) |
| 表情不切换 | 事件未发布/状态错误 | [MODULE_EVENT.md](docs/MODULE_EVENT.md) |
| 语音断续 | 预缓冲不足/队列/阻塞 | [MODULE_AUDIO.md](docs/MODULE_AUDIO.md) 第13节 |
| 状态卡住 | 状态机竞态 | [MODULE_APPLICATION.md](docs/MODULE_APPLICATION.md) |
| AT 超时 | ML307 阻塞 | 见下方 ML307 问题 |

### TTS 无声音排查

**服务器日志**:
```bash
journalctl -u xiaozhi-api --since "5 min ago" | grep -iE "TTS|Opus|format"
```

**正确日志**: `Detected format: Ogg/Opus` + `X Opus frames`

**错误对照**:
| 日志 | 原因 | 修复 |
|------|------|------|
| `Detected format: MP3` | 格式配置错误 | TTS_FORMAT=opus |
| `TTS failed: 418` | 音色不匹配 | 用 `_v2` 后缀音色 |
| `0 Opus frames` | Ogg 解析失败 | 检查 OggOpusParser |

**正确配置 (.env)**:
```bash
TTS_MODEL=cosyvoice-v2
TTS_VOICE=longxiaochun_v2
TTS_FORMAT=opus
TTS_SAMPLE_RATE=16000
```

### ML307 AT 超时

**日志**: `E AtUart: << CMD TIMEOUT`

**原因**: 同步操作阻塞 ReceiveTask

**方案**: 使用异步队列 (PongQueue)，增加超时到 3000ms

### 状态竞态问题

**现象**: 服务器发音频，设备不播放

**根因**: `Schedule()` 异步执行，音频到达时仍在旧状态

**修复**: `OnIncomingAudio` 中同时接受 `Listening` 和 `Speaking`

### 4G 音频播放断续 (重要!)

**现象**: TTS 播放断断续续，或长时间播放良好但结尾卡顿

**日志特征**:
```
W AtUart: [URC] Queue full, dropping: MIPURC  # URC 队列溢出
W AudioService: Buffer underrun  # 缓冲耗尽
W AudioService: Decode queue full  # 队列满丢包
```

**根因**: 4G 模块特殊架构
- WebSocket 数据回调与 URC 处理共用线程
- 无限阻塞会导致 URC 队列溢出，丢失 AUDIO_END

**解决方案** (已实现):
1. 队列容量: 60 → 200 包 (12秒缓冲)
2. 阻塞方式: `cv.wait()` → `cv.wait_for(100ms)`
3. 预缓冲: 180ms → 600ms

**详细文档**: [MODULE_AUDIO.md 第13节](docs/MODULE_AUDIO.md)

### 4G 音频三大遗留问题方案 (待实施)

**问题**: 丢包爆音 / 打断延迟 / NAT 超时断连

**根因**: ML307 AT 串口单线程，数据面与控制面耦合

**方案**: 控制面/数据面分离 — 数据帧走 AT+MIPSEND，Ping/Pong 走被动响应，序列号嵌入 reserved 字段

**详细文档**: [MODULE_AUDIO.md 第14节](docs/MODULE_AUDIO.md)

### Always Online 常驻在线模式

**功能**: 设备启动后自动连接服务器，保持在线状态，不进入待机模式。

**启用方式**:
```
CONFIG_ALWAYS_ONLINE=y  # 在 sdkconfig 或 menuconfig 中启用
```

**实现要点**:

1. **启动自动连接** (`application.cc`):
   - 在 `protocol_->Start()` 后调用 `OpenAudioChannel()`
   - 成功后进入 Listening 状态

2. **断线自动重连** (`OnAudioChannelClosed` 回调):
   - WebSocket 断开后延迟 1 秒自动重连
   - 避免频繁重试导致资源耗尽

3. **保持监听状态**:
   - TTS 播放完毕后保持 Listening（不回到 Idle）
   - 按键停止监听后保持连接等待响应

**关键代码位置**:
```cpp
// application.cc - 启动自动连接
#if CONFIG_ALWAYS_ONLINE
    ESP_LOGI(TAG, "Always Online mode enabled, auto-connecting to server...");
    Schedule([this]() {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateListening);
            }
        }
    });
#endif
```

**经验总结**:
- Kconfig 选项添加后需重新编译才生效
- 使用条件编译 `#if CONFIG_ALWAYS_ONLINE` 保持向后兼容
- 断线重连需要延迟，避免 4G 模块连接风暴

---

## 禁止操作 ⚠️

> **严格禁止**: 以下命令会导致终端卡死，必须避免使用！

| 命令 | 原因 | 替代方案 |
|------|------|----------|
| `stty -F /dev/ttyACM0` | **卡死终端** - WSL 环境下会永久阻塞 | 直接用 `cat /dev/ttyACM0` |
| `stty` + 任何串口操作 | 同上 | `timeout X cat /dev/ttyACM0` |
| `miniterm` | WSL 不可用 | `timeout 30 cat /dev/ttyACM0 \| head -200` |
| `screen /dev/ttyACM0` | 可能卡死 | 同上 |

**正确的串口读取方式**:
```bash
# 读取串口日志 (推荐)
timeout 30 cat /dev/ttyACM0 2>&1 | head -200

# 过滤关键日志
timeout 30 cat /dev/ttyACM0 2>&1 | strings | grep -iE "error|state|connect"
```

---

## 后端服务

| 服务 | 地址 |
|------|------|
| API | `http://47.109.187.90:6100` |
| WebSocket | `ws://47.109.187.90:6100/api/v1/ws?token=xxx` |
| 管理后台 | `http://47.109.187.90` |

**服务器日志**:
```bash
sshpass -p 'Maker5644014' ssh root@47.109.187.90 'journalctl -u xiaozhi-api -n 100 --no-pager'
```

---

## 快速代码片段

### 发布事件
```cpp
#include "core/event_bridge.h"

EventBridge::EmitSetEmotion("happy");
EventBridge::EmitAudioOutputStart();
EventBridge::EmitConnectionSuccess();
```

### 调度任务到主循环
```cpp
Application::GetInstance().Schedule([this]() {
    SetDeviceState(kDeviceStateListening);
});
```

### 推送音频到播放队列
```cpp
auto packet = std::make_unique<AudioStreamPacket>();
packet->payload = opus_data;
audio_service_.PushPacketToDecodeQueue(std::move(packet));
```

---

## 版本信息

- **ESP-IDF**: v5.4.1
- **芯片**: ESP32-S3
- **协议版本**: BinaryProtocol3
