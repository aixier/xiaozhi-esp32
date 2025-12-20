# 小智 AI 服务端 API 规范文档

本文档描述了小智 ESP32 客户端与服务端之间的通信协议规范，包括 OTA 接口、WebSocket/MQTT 实时通信协议、MCP 工具调用协议。

---

## 目录

1. [OTA 配置接口](#1-ota-配置接口)
2. [设备激活接口](#2-设备激活接口)
3. [WebSocket 实时通信协议](#3-websocket-实时通信协议)
4. [MQTT + UDP 混合协议](#4-mqtt--udp-混合协议)
5. [MCP 工具调用协议](#5-mcp-工具调用协议)
6. [消息类型汇总](#6-消息类型汇总)
7. [二进制音频协议](#7-二进制音频协议)

---

## 1. OTA 配置接口

### 1.1 检查版本 / 获取配置

设备启动时调用此接口获取服务器配置和固件更新信息。

**请求**

```
POST/GET {OTA_URL}
默认: https://api.tenclass.net/xiaozhi/ota/
```

**请求头**

| Header | 说明 | 示例 |
|--------|------|------|
| `Activation-Version` | 激活版本 (1 或 2) | `2` |
| `Device-Id` | 设备 MAC 地址 | `AA:BB:CC:DD:EE:FF` |
| `Client-Id` | 设备 UUID | `uuid-string` |
| `Serial-Number` | 序列号 (版本2时) | `SN123456` |
| `User-Agent` | 设备信息 | `zhengchen-eye/1.8.5` |
| `Accept-Language` | 语言代码 | `zh-CN` |
| `Content-Type` | 内容类型 | `application/json` |

**请求体 (POST)**

```json
{
    "board": "zhengchen-eye",
    "chip": "esp32s3",
    "flash_size": 16777216,
    "psram_size": 8388608,
    "features": ["wifi", "lcd", "mcp"]
}
```

**响应**

```json
{
    "firmware": {
        "version": "1.8.6",
        "url": "https://example.com/firmware.bin"
    },
    "activation": {
        "message": "请输入激活码",
        "code": "ABC123",
        "challenge": "random-challenge-string",
        "timeout_ms": 30000
    },
    "mqtt": {
        "endpoint": "mqtt.example.com:8883",
        "client_id": "device_123",
        "username": "user",
        "password": "pass",
        "publish_topic": "device/123/up",
        "subscribe_topic": "device/123/down",
        "keepalive": 240
    },
    "websocket": {
        "url": "wss://api.example.com/ws",
        "token": "Bearer xxx",
        "version": 3
    },
    "server_time": {
        "timestamp": 1702900000000,
        "timezone_offset": 480
    }
}
```

**响应字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `firmware.version` | string | 最新固件版本号 |
| `firmware.url` | string | 固件下载地址 |
| `activation.code` | string | 激活码（显示给用户） |
| `activation.challenge` | string | HMAC 挑战字符串 |
| `activation.timeout_ms` | int | 激活超时时间（毫秒） |
| `mqtt.*` | object | MQTT 连接配置 |
| `websocket.*` | object | WebSocket 连接配置 |
| `server_time.timestamp` | number | 服务器时间戳（毫秒） |
| `server_time.timezone_offset` | int | 时区偏移（分钟） |

---

## 2. 设备激活接口

### 2.1 激活请求

**请求**

```
POST {OTA_URL}/activate
```

**请求头**

同 OTA 配置接口。

**请求体**

```json
{
    "algorithm": "hmac-sha256",
    "serial_number": "SN123456",
    "challenge": "random-challenge-string",
    "hmac": "64位十六进制HMAC值"
}
```

**响应**

| HTTP 状态码 | 说明 |
|-------------|------|
| 200 | 激活成功 |
| 202 | 等待中（需要用户确认） |
| 4xx/5xx | 激活失败 |

---

## 3. WebSocket 实时通信协议

### 3.1 连接建立

**连接地址**

```
wss://{websocket.url}
```

**连接头**

| Header | 说明 |
|--------|------|
| `Authorization` | Bearer Token |
| `Protocol-Version` | 协议版本 (1/2/3) |
| `Device-Id` | 设备 MAC 地址 |
| `Client-Id` | 设备 UUID |

### 3.2 客户端 Hello 消息

连接成功后，客户端首先发送 hello 消息。

**请求**

```json
{
    "type": "hello",
    "version": 3,
    "transport": "websocket",
    "features": {
        "aec": true,
        "mcp": true
    },
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | 消息类型，固定为 `hello` |
| `version` | int | 协议版本 (1/2/3) |
| `transport` | string | 传输方式 `websocket` |
| `features.aec` | bool | 是否启用服务器端回声消除 |
| `features.mcp` | bool | 是否支持 MCP 协议 |
| `audio_params.format` | string | 音频格式，固定为 `opus` |
| `audio_params.sample_rate` | int | 采样率，16000 Hz |
| `audio_params.channels` | int | 声道数，1 |
| `audio_params.frame_duration` | int | 帧时长，60 ms |

### 3.3 服务端 Hello 响应

**响应**

```json
{
    "type": "hello",
    "transport": "websocket",
    "session_id": "session-uuid-string",
    "audio_params": {
        "sample_rate": 24000,
        "frame_duration": 60
    }
}
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | 会话 ID，后续消息需携带 |
| `audio_params.sample_rate` | int | 服务端音频采样率 |
| `audio_params.frame_duration` | int | 服务端音频帧时长 |

### 3.4 唤醒词检测消息

客户端检测到唤醒词后发送。

**请求**

```json
{
    "session_id": "session-uuid",
    "type": "listen",
    "state": "detect",
    "text": "你好小智"
}
```

### 3.5 开始监听消息

**请求**

```json
{
    "session_id": "session-uuid",
    "type": "listen",
    "state": "start",
    "mode": "auto"
}
```

**mode 字段**

| 值 | 说明 |
|----|------|
| `auto` | 自动停止模式（VAD 检测） |
| `manual` | 手动停止模式 |
| `realtime` | 实时模式（需要 AEC 支持） |

### 3.6 停止监听消息

**请求**

```json
{
    "session_id": "session-uuid",
    "type": "listen",
    "state": "stop"
}
```

### 3.7 中止播放消息

**请求**

```json
{
    "session_id": "session-uuid",
    "type": "abort",
    "reason": "wake_word_detected"
}
```

**reason 字段**

| 值 | 说明 |
|----|------|
| `wake_word_detected` | 检测到唤醒词 |
| (空) | 用户手动中止 |

### 3.8 服务端消息类型

#### 3.8.1 TTS 消息

**开始播放**

```json
{
    "type": "tts",
    "state": "start"
}
```

**句子开始**

```json
{
    "type": "tts",
    "state": "sentence_start",
    "text": "你好，有什么可以帮助你的吗？"
}
```

**停止播放**

```json
{
    "type": "tts",
    "state": "stop"
}
```

#### 3.8.2 STT 消息

语音识别结果。

```json
{
    "type": "stt",
    "text": "今天天气怎么样"
}
```

#### 3.8.3 LLM 消息

大模型响应，包含情绪信息。

```json
{
    "type": "llm",
    "emotion": "happy"
}
```

**emotion 字段取值**

| 值 | 说明 |
|----|------|
| `neutral` | 中性 |
| `happy` | 开心 |
| `sad` | 悲伤 |
| `angry` | 愤怒 |
| `surprised` | 惊讶 |
| `fearful` | 恐惧 |
| `disgusted` | 厌恶 |
| `confused` | 困惑 |
| `thinking` | 思考中 |

#### 3.8.4 System 消息

系统命令。

```json
{
    "type": "system",
    "command": "reboot"
}
```

**command 字段取值**

| 值 | 说明 |
|----|------|
| `reboot` | 重启设备 |

#### 3.8.5 Alert 消息

警告/通知消息。

```json
{
    "type": "alert",
    "status": "warning",
    "message": "电池电量低",
    "emotion": "sad"
}
```

#### 3.8.6 MCP 消息

MCP 工具调用消息（详见第5节）。

```json
{
    "type": "mcp",
    "payload": {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "id": 1,
        "params": {
            "name": "self.audio_speaker.set_volume",
            "arguments": {
                "volume": 50
            }
        }
    }
}
```

#### 3.8.7 Custom 消息

自定义消息（需开启 CONFIG_RECEIVE_CUSTOM_MESSAGE）。

```json
{
    "type": "custom",
    "payload": {
        "custom_field": "custom_value"
    }
}
```

---

## 4. MQTT + UDP 混合协议

适用于网络不稳定场景，MQTT 用于控制消息，UDP 用于音频流。

### 4.1 MQTT 连接

使用 OTA 接口返回的 MQTT 配置连接。

### 4.2 客户端 Hello 消息

通过 MQTT publish_topic 发送。

**请求**

```json
{
    "type": "hello",
    "version": 3,
    "transport": "udp",
    "features": {
        "aec": true,
        "mcp": true
    },
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

### 4.3 服务端 Hello 响应

**响应**

```json
{
    "type": "hello",
    "transport": "udp",
    "session_id": "session-uuid",
    "audio_params": {
        "sample_rate": 24000,
        "frame_duration": 60
    },
    "udp": {
        "server": "udp.example.com",
        "port": 12345,
        "key": "32位十六进制AES密钥",
        "nonce": "32位十六进制随机数"
    }
}
```

**UDP 加密参数**

| 字段 | 说明 |
|------|------|
| `server` | UDP 服务器地址 |
| `port` | UDP 服务器端口 |
| `key` | AES-128-CTR 密钥（十六进制） |
| `nonce` | AES-CTR 初始向量（十六进制） |

### 4.4 Goodbye 消息

关闭音频通道。

**请求**

```json
{
    "session_id": "session-uuid",
    "type": "goodbye"
}
```

**服务端主动关闭**

```json
{
    "type": "goodbye",
    "session_id": "session-uuid"
}
```

### 4.5 UDP 加密音频包格式

```
|type 1u|flags 1u|payload_len 2u|ssrc 4u|timestamp 4u|sequence 4u|payload...|
```

| 字段 | 大小 | 说明 |
|------|------|------|
| type | 1 字节 | 包类型，0x01 = 音频 |
| flags | 1 字节 | 保留标志 |
| payload_len | 2 字节 | 负载长度（网络字节序） |
| ssrc | 4 字节 | 同步源标识 |
| timestamp | 4 字节 | 时间戳（毫秒，网络字节序） |
| sequence | 4 字节 | 序列号（网络字节序） |
| payload | N 字节 | AES-CTR 加密的 OPUS 数据 |

---

## 5. MCP 工具调用协议

基于 [Model Context Protocol](https://modelcontextprotocol.io/specification/2024-11-05) 规范。

### 5.1 协议概述

MCP 消息通过 WebSocket/MQTT 的 `mcp` 消息类型传输。

**客户端发送格式**

```json
{
    "session_id": "session-uuid",
    "type": "mcp",
    "payload": { /* JSON-RPC 2.0 请求/响应 */ }
}
```

### 5.2 Initialize 请求

服务端初始化 MCP 连接。

**请求 (服务端 → 设备)**

```json
{
    "jsonrpc": "2.0",
    "method": "initialize",
    "id": 1,
    "params": {
        "capabilities": {
            "vision": {
                "url": "https://vision.example.com/api",
                "token": "vision-api-token"
            }
        }
    }
}
```

**响应 (设备 → 服务端)**

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
        "protocolVersion": "2024-11-05",
        "capabilities": {
            "tools": {}
        },
        "serverInfo": {
            "name": "zhengchen-eye",
            "version": "1.8.5"
        }
    }
}
```

### 5.3 tools/list 请求

获取设备支持的工具列表。

**请求**

```json
{
    "jsonrpc": "2.0",
    "method": "tools/list",
    "id": 2,
    "params": {
        "cursor": ""
    }
}
```

**响应**

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
        "tools": [
            {
                "name": "self.get_device_status",
                "description": "Provides the real-time information of the device...",
                "inputSchema": {
                    "type": "object",
                    "properties": {}
                }
            },
            {
                "name": "self.audio_speaker.set_volume",
                "description": "Set the volume of the audio speaker...",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "volume": {
                            "type": "integer",
                            "minimum": 0,
                            "maximum": 100
                        }
                    },
                    "required": ["volume"]
                }
            },
            {
                "name": "self.screen.set_brightness",
                "description": "Set the brightness of the screen.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "brightness": {
                            "type": "integer",
                            "minimum": 0,
                            "maximum": 100
                        }
                    },
                    "required": ["brightness"]
                }
            },
            {
                "name": "self.screen.set_theme",
                "description": "Set the theme of the screen. The theme can be `light` or `dark`.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "theme": {
                            "type": "string"
                        }
                    },
                    "required": ["theme"]
                }
            },
            {
                "name": "self.camera.take_photo",
                "description": "Take a photo and explain it.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "question": {
                            "type": "string"
                        }
                    },
                    "required": ["question"]
                }
            }
        ],
        "nextCursor": "tool_name_for_pagination"
    }
}
```

**分页说明**

- 单次最大返回约 8000 字节
- 如有更多工具，`nextCursor` 包含下一页起始工具名
- 客户端需使用 `cursor` 参数继续获取

### 5.4 tools/call 请求

调用设备工具。

**请求**

```json
{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "id": 3,
    "params": {
        "name": "self.audio_speaker.set_volume",
        "arguments": {
            "volume": 50
        },
        "stackSize": 6144
    }
}
```

**参数说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 工具名称 |
| `arguments` | object | 工具参数 |
| `stackSize` | int | 可选，执行任务栈大小（默认 6144） |

**成功响应**

```json
{
    "jsonrpc": "2.0",
    "id": 3,
    "result": {
        "content": [
            {
                "type": "text",
                "text": "true"
            }
        ]
    }
}
```

**返回值类型**

| 类型 | 说明 | 示例 |
|------|------|------|
| boolean | 布尔值 | `true` / `false` |
| integer | 整数 | `50` |
| string | 字符串 | `"success"` |
| JSON | JSON 对象 | `{"status": "ok"}` |

**错误响应**

```json
{
    "jsonrpc": "2.0",
    "id": 3,
    "error": {
        "message": "Unknown tool: invalid_tool"
    }
}
```

### 5.5 内置工具列表

| 工具名称 | 描述 | 参数 |
|----------|------|------|
| `self.get_device_status` | 获取设备状态 | 无 |
| `self.get_head_status` | 获取头部触摸数据 | 无 |
| `self.get_body_status` | 获取身体触摸数据 | 无 |
| `self.audio_speaker.set_volume` | 设置音量 | `volume`: 0-100 |
| `self.screen.set_brightness` | 设置屏幕亮度 | `brightness`: 0-100 |
| `self.screen.set_theme` | 设置主题 | `theme`: "light"/"dark" |
| `self.camera.take_photo` | 拍照并识别 | `question`: string |

---

## 6. 消息类型汇总

### 6.1 客户端 → 服务端

| type | state/其他 | 说明 |
|------|------------|------|
| `hello` | - | 客户端握手 |
| `listen` | `detect` | 唤醒词检测 |
| `listen` | `start` | 开始监听 |
| `listen` | `stop` | 停止监听 |
| `abort` | - | 中止播放 |
| `goodbye` | - | 关闭会话 |
| `mcp` | - | MCP 响应 |

### 6.2 服务端 → 客户端

| type | state/其他 | 说明 |
|------|------------|------|
| `hello` | - | 服务端握手响应 |
| `tts` | `start` | TTS 开始 |
| `tts` | `sentence_start` | 句子开始 |
| `tts` | `stop` | TTS 结束 |
| `stt` | - | 语音识别结果 |
| `llm` | - | LLM 响应（含情绪） |
| `system` | - | 系统命令 |
| `alert` | - | 警告消息 |
| `mcp` | - | MCP 请求 |
| `custom` | - | 自定义消息 |
| `goodbye` | - | 关闭会话 |

---

## 7. 二进制音频协议

### 7.1 Protocol Version 1

原始 OPUS 数据，无包头。

```
|opus_data...|
```

### 7.2 Protocol Version 2

```c
struct BinaryProtocol2 {
    uint16_t version;       // 版本号（网络字节序）
    uint16_t type;          // 消息类型: 0=OPUS, 1=JSON
    uint32_t reserved;      // 保留字段
    uint32_t timestamp;     // 时间戳（毫秒，用于 AEC）
    uint32_t payload_size;  // 负载大小（网络字节序）
    uint8_t  payload[];     // 负载数据
} __attribute__((packed));
```

**总大小**: 16 字节头 + payload

### 7.3 Protocol Version 3 (推荐)

轻量级协议，减少带宽占用。

```c
struct BinaryProtocol3 {
    uint8_t  type;          // 消息类型: 0=OPUS
    uint8_t  reserved;      // 保留字段
    uint16_t payload_size;  // 负载大小（网络字节序）
    uint8_t  payload[];     // 负载数据
} __attribute__((packed));
```

**总大小**: 4 字节头 + payload

### 7.4 音频参数

| 参数 | 客户端 → 服务端 | 服务端 → 客户端 | 备注 |
|------|-----------------|-----------------|------|
| 格式 | OPUS | OPUS | **必须匹配！** 设备只有 Opus 解码器 |
| 采样率 | 16000 Hz | 16000 Hz | **必须匹配！** 设备期望 16000 Hz |
| 声道数 | 1 (单声道) | 1 (单声道) | |
| 帧时长 | 60 ms | 60 ms | |

**服务端 TTS 配置 (config.py)**:
```python
tts_format: str = "opus"      # 必须是 opus，不能用 mp3/wav
tts_sample_rate: int = 16000  # 必须是 16000，不能用 22050/24000
```

**注意**: 设备端 (ESP32) 只内置 Opus 解码器，无法解码 MP3/WAV 等其他格式。如果 TTS 输出非 Opus 格式，设备将无法播放音频。

---

## 8. 错误处理

### 8.1 连接超时

- WebSocket 连接等待服务端 hello 超时: 10 秒
- 音频通道空闲超时: 120 秒

### 8.2 网络错误码

| 错误 | 说明 |
|------|------|
| SERVER_NOT_FOUND | 服务器未配置 |
| SERVER_NOT_CONNECTED | 连接失败 |
| SERVER_TIMEOUT | 服务器响应超时 |
| SERVER_ERROR | 服务器内部错误 |

---

## 9. 完整通信流程示例

### 9.1 WebSocket 语音对话流程

```
客户端                                  服务端
   |                                      |
   |------ WebSocket 连接建立 ----------->|
   |                                      |
   |------ hello (client) --------------->|
   |<----- hello (server) ----------------|
   |                                      |
   |------ listen (detect) -------------->| 唤醒词检测
   |------ listen (start, auto) --------->| 开始监听
   |------ 二进制音频流 ------------------>|
   |------ 二进制音频流 ------------------>|
   |                                      |
   |<----- stt (text) --------------------| 识别结果
   |                                      |
   |<----- tts (start) -------------------| 开始播放
   |<----- tts (sentence_start) ----------| 句子文本
   |<----- 二进制音频流 -------------------|
   |<----- llm (emotion) -----------------| 情绪变化
   |<----- 二进制音频流 -------------------|
   |<----- tts (stop) --------------------| 播放结束
   |                                      |
   |------ listen (start, auto) --------->| 继续监听
   |                                      |
```

### 9.2 MCP 工具调用流程

```
客户端                                  服务端                    大模型
   |                                      |                         |
   |                                      |<---- "调低音量" ---------|
   |                                      |                         |
   |<----- mcp (tools/list) --------------|                         |
   |------ mcp (tools list result) ------>|                         |
   |                                      |------ 工具列表 --------->|
   |                                      |                         |
   |                                      |<---- 调用 set_volume ----|
   |<----- mcp (tools/call) --------------|                         |
   |                                      |                         |
   |------ 执行 set_volume(50) ---------->|                         |
   |                                      |                         |
   |------ mcp (result: true) ----------->|                         |
   |                                      |------ 执行成功 --------->|
   |                                      |                         |
   |<----- tts (已为您调低音量) -----------|<---- 回复用户 ----------|
   |                                      |                         |
```

---

## 附录 A: 服务端开发指南

### A.1 必须实现的接口

1. **OTA 配置接口** - 返回 WebSocket/MQTT 连接配置
2. **WebSocket 或 MQTT 服务** - 实时通信
3. **音频处理** - OPUS 编解码、ASR、TTS
4. **LLM 对接** - 大模型调用

### A.2 可选实现

1. **MCP 工具调用** - 设备控制能力
2. **服务端 AEC** - 回声消除
3. **UDP 音频通道** - 低延迟传输

### A.3 开源服务端实现

| 语言 | 项目地址 |
|------|----------|
| Python | [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) |
| Java | [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) |
| Golang | [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) |

---

---

## 10. 管理后台 API

小智平台提供 RESTful 管理 API，用于设备管理、告警推送等功能。

### 10.1 认证

**登录**

```
POST /api/v1/admin/auth/login
Content-Type: application/json

{
    "username": "admin",
    "password": "password"
}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
        "expires_at": "2025-12-21T03:15:59.543382",
        "user": {
            "id": 1,
            "username": "admin",
            "role": "admin"
        }
    }
}
```

后续请求需携带 Authorization 头：
```
Authorization: Bearer {token}
```

### 10.2 设备管理

**获取设备列表**

```
GET /api/v1/admin/devices
Authorization: Bearer {token}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "total": 1,
        "page": 1,
        "page_size": 20,
        "items": [
            {
                "id": 7,
                "device_id": "1f89ddf2-9efd-47a9-9600-beedb3bce0df",
                "client_id": "24b5fd04-68b1-54dd-8124-dcb4d92401f8",
                "name": "小智",
                "status": "online",
                "firmware_version": "1.8.5",
                "last_seen_at": "2025-12-19T19:14:16.297943Z",
                "created_at": "2025-12-19T13:33:55.509678Z"
            }
        ]
    }
}
```

**设备状态**

| status | 说明 |
|--------|------|
| `online` | 在线 |
| `offline` | 离线 |

### 10.3 告警推送 API

向指定设备推送告警消息，消息会显示在设备屏幕上。

**请求**

```
POST /api/v1/admin/devices/{device_id}/alert
Authorization: Bearer {token}
Content-Type: application/json
```

**注意**：`device_id` 使用 UUID 格式 (如 `1f89ddf2-9efd-47a9-9600-beedb3bce0df`)，不是数据库 ID。

**请求体**

```json
{
    "status": "告警标题",
    "message": "这是一条测试告警消息",
    "emotion": "happy"
}
```

**参数说明**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 是 | 告警标题，显示在状态栏 |
| `message` | string | 是 | 告警内容，显示在聊天区域 |
| `emotion` | string | 否 | 表情，默认 `neutral` |

**emotion 可选值**

| 值 | 说明 |
|----|------|
| `neutral` | 中性 |
| `happy` | 开心 |
| `sad` | 悲伤 |
| `angry` | 愤怒 |
| `surprised` | 惊讶 |
| `thinking` | 思考中 |
| `confused` | 困惑 |
| `sleepy` | 困倦 |

**成功响应**

```json
{
    "code": 0,
    "message": "警告发送成功",
    "data": {
        "device_id": "1f89ddf2-9efd-47a9-9600-beedb3bce0df",
        "status": "告警标题",
        "message": "这是一条测试告警消息",
        "emotion": "happy",
        "sent": true
    }
}
```

**设备端处理**

告警消息通过 WebSocket 以 `alert` 类型发送到设备：

```json
{
    "type": "alert",
    "status": "告警标题",
    "message": "这是一条测试告警消息",
    "emotion": "happy"
}
```

设备收到后会：
1. 更新状态栏显示 `status`
2. 设置表情为 `emotion`
3. 在聊天区域显示 `message` (需启用 `CONFIG_USE_WECHAT_MESSAGE_STYLE`)
4. 播放提示音

---

**文档版本**: 1.1
**协议版本**: 2024-11-05 (MCP)
**最后更新**: 2025-12-20
**维护者**: Claude Code
