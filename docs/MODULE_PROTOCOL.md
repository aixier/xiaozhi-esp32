# Protocol 模块分析

## 1. 模块概述

Protocol 层负责设备与服务器之间的通信，支持：
- WebSocket 双向通信
- 二进制协议 (BinaryProtocol3)
- JSON 控制消息
- 心跳保活

## 2. 类层次

```
┌─────────────────────────────────────────────┐
│                 Protocol                     │  抽象基类
│  - 定义通信接口                              │
│  - 提供公共消息方法                          │
└─────────────────────────────────────────────┘
                      △
                      │
         ┌────────────┴────────────┐
         │                         │
┌─────────────────┐      ┌─────────────────┐
│WebsocketProtocol│      │  MqttProtocol   │
│ - WebSocket 实现│      │ - MQTT 实现     │
└─────────────────┘      └─────────────────┘
```

## 3. Protocol 抽象接口

```cpp
class Protocol {
public:
    // 生命周期
    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
    virtual bool IsAudioChannelOpened() const = 0;

    // 数据发送
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;
    virtual void SendWakeWordDetected(const std::string& wake_word);
    virtual void SendStartListening(ListeningMode mode);
    virtual void SendStopListening();
    virtual void SendAbortSpeaking(AbortReason reason);
    virtual void SendMcpMessage(const std::string& message);

    // 回调注册
    void OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket>)>);
    void OnIncomingJson(std::function<void(const cJSON*)>);
    void OnAudioChannelOpened(std::function<void()>);
    void OnAudioChannelClosed(std::function<void()>);
    void OnNetworkError(std::function<void(const std::string&)>);

protected:
    virtual bool SendText(const std::string& text) = 0;

    // 服务器参数
    int server_sample_rate_ = 24000;
    int server_frame_duration_ = 60;
    std::string session_id_;
};
```

## 4. BinaryProtocol3 格式

### 4.1 协议结构

```
┌─────────┬──────────┬──────────────┬─────────────────┐
│  type   │ reserved │ payload_size │     payload     │
│ 1 byte  │ 1 byte   │  2 bytes     │   N bytes       │
└─────────┴──────────┴──────────────┴─────────────────┘
```

```cpp
struct BinaryProtocol3 {
    uint8_t type;           // 消息类型
    uint8_t reserved;       // 保留字段
    uint16_t payload_size;  // 载荷长度 (网络字节序)
    uint8_t payload[];      // 载荷数据
} __attribute__((packed));
```

### 4.2 消息类型定义

| 类型 | 值 | 方向 | 载荷 | 说明 |
|------|-----|------|------|------|
| AUDIO_DATA | 0x00 | 双向 | Opus 帧 | 音频数据 |
| AUDIO_START | 0x10 | 服务器→设备 | 无 | TTS 开始 |
| AUDIO_DATA | 0x11 | 服务器→设备 | Opus 帧 | TTS 数据 |
| AUDIO_END | 0x12 | 服务器→设备 | 无 | TTS 结束 |
| TEXT_ASR | 0x20 | 服务器→设备 | JSON | ASR 结果 |
| TEXT_LLM | 0x21 | 服务器→设备 | JSON | LLM 响应 |
| ERROR | 0x0F | 服务器→设备 | JSON | 错误信息 |

### 4.3 JSON 载荷格式

```json
// TEXT_ASR (0x20)
{"text": "你好", "is_final": true}

// TEXT_LLM (0x21)
{"text": "你好，", "is_final": false}
{"text": "", "is_final": true}

// ERROR (0x0F)
{"code": 500, "message": "Internal error"}
```

## 5. WebsocketProtocol 实现

### 5.1 连接流程

```
设备                                     服务器
  │                                        │
  │──────── WebSocket Connect ────────────▶│
  │                                        │
  │◀─────── HTTP 101 Switching ────────────│
  │                                        │
  │──────── Hello (JSON) ─────────────────▶│
  │  {"type":"hello","version":3,...}      │
  │                                        │
  │◀─────── Hello Response (JSON) ─────────│
  │  {"type":"hello","session_id":"..."}   │
  │                                        │
  │═══════ 连接建立完成 ═══════════════════│
```

### 5.2 Hello 消息

```cpp
std::string WebsocketProtocol::GetHelloMessage() {
    // 构建 JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 3);
    cJSON_AddStringToObject(root, "transport", "websocket");

    // 音频参数
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", 60);
    cJSON_AddItemToObject(root, "audio_params", audio_params);

    // 特性声明
    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);

    return cJSON_PrintUnformatted(root);
}
```

### 5.3 数据发送

```cpp
bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!websocket_ || !websocket_->IsConnected()) {
        return false;
    }

    // 构建 BinaryProtocol3 帧
    std::string serialized;
    serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());

    auto* bp3 = (BinaryProtocol3*)serialized.data();
    bp3->type = 0x00;  // AUDIO_DATA
    bp3->reserved = 0;
    bp3->payload_size = htons(packet->payload.size());
    memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

    return websocket_->Send(serialized.data(), serialized.size(), true);
}
```

### 5.4 数据接收

```cpp
websocket_->OnData([this](const char* data, size_t len, bool binary) {
    if (binary) {
        BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
        uint8_t msg_type = bp3->type;
        uint16_t payload_size = ntohs(bp3->payload_size);
        uint8_t* payload = bp3->payload;

        switch (msg_type) {
            case 0x10:  // AUDIO_START
                if (on_incoming_json_) {
                    cJSON* root = cJSON_CreateObject();
                    cJSON_AddStringToObject(root, "type", "tts");
                    cJSON_AddStringToObject(root, "state", "start");
                    on_incoming_json_(root);
                    cJSON_Delete(root);
                }
                break;

            case 0x11:  // AUDIO_DATA
                if (on_incoming_audio_) {
                    auto packet = std::make_unique<AudioStreamPacket>();
                    packet->sample_rate = server_sample_rate_;
                    packet->frame_duration = server_frame_duration_;
                    packet->payload.assign(payload, payload + payload_size);
                    on_incoming_audio_(std::move(packet));
                }
                break;

            case 0x12:  // AUDIO_END
                if (on_incoming_json_) {
                    cJSON* root = cJSON_CreateObject();
                    cJSON_AddStringToObject(root, "type", "tts");
                    cJSON_AddStringToObject(root, "state", "stop");
                    on_incoming_json_(root);
                    cJSON_Delete(root);
                }
                break;

            case 0x20:  // TEXT_ASR
            case 0x21:  // TEXT_LLM
                // 解析 JSON 载荷
                break;
        }
    } else {
        // JSON 文本消息
        cJSON* root = cJSON_Parse(data);
        if (root) {
            cJSON* type = cJSON_GetObjectItem(root, "type");
            if (strcmp(type->valuestring, "hello") == 0) {
                ParseServerHello(root);
            } else {
                on_incoming_json_(root);
            }
            cJSON_Delete(root);
        }
    }

    // 更新最后活动时间
    last_incoming_time_ = std::chrono::steady_clock::now();
});
```

## 6. JSON 控制消息

### 6.1 Listen 消息

```cpp
void Protocol::SendStartListening(ListeningMode mode) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";

    switch (mode) {
        case kListeningModeRealtime:
            message += ",\"mode\":\"realtime\"";
            break;
        case kListeningModeAutoStop:
            message += ",\"mode\":\"auto\"";
            break;
        default:
            message += ",\"mode\":\"manual\"";
            break;
    }

    message += "}";
    SendText(message);
}
```

### 6.2 Abort 消息

```cpp
void Protocol::SendAbortSpeaking(AbortReason reason) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}
```

## 7. WebSocket 底层

### 7.1 ML307 4G WebSocket

```cpp
class WebSocket {
public:
    WebSocket(NetworkInterface* network, int connect_id);

    // 配置
    void SetHeader(const char* key, const char* value);
    void SetReceiveBufferSize(size_t size);

    // 连接管理
    bool Connect(const char* uri);
    void Close();
    bool IsConnected() const;

    // 数据传输
    bool Send(const std::string& data);
    bool Send(const void* data, size_t len, bool binary = false, bool fin = true);
    void Ping();

    // 回调
    void OnConnected(std::function<void()>);
    void OnDisconnected(std::function<void()>);
    void OnData(std::function<void(const char*, size_t, bool)>);
    void OnError(std::function<void(int)>);

private:
    // PING/PONG 异步处理
    QueueHandle_t pong_queue_;
    TaskHandle_t pong_task_handle_;

    void EnqueuePong(const char* payload, size_t len);
    void PongSendTask();
};
```

### 7.2 PING/PONG 处理

```cpp
// 收到 PING 时入队
case 0x9:  // PING
    if (handshake_completed_ && connected_) {
        EnqueuePong(payload.data(), payload_length);
    }
    break;

// 异步发送 PONG
void WebSocket::PongSendTask() {
    PongMessage* msg = nullptr;
    while (true) {
        if (xQueueReceive(pong_queue_, &msg, portMAX_DELAY) == pdTRUE) {
            if (connected_ && tcp_) {
                SendControlFrame(0xA, msg->payload.data(), msg->payload.size());
            }
            delete msg;
        }
    }
}
```

## 8. 超时处理

```cpp
bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_incoming_time_
    );
    return duration.count() > kTimeoutSeconds;
}
```

## 9. 时序图

### 9.1 完整对话流程

```
Device                  Server                    LLM/TTS
  │                       │                          │
  │──── listen:start ────▶│                          │
  │──── audio_data ──────▶│                          │
  │──── audio_data ──────▶│                          │
  │──── listen:stop ─────▶│                          │
  │                       │────── ASR ──────────────▶│
  │◀─── TEXT_ASR ─────────│                          │
  │                       │◀──── LLM Response ───────│
  │◀─── TEXT_LLM ─────────│                          │
  │                       │◀──── TTS Audio ──────────│
  │◀─── AUDIO_START ──────│                          │
  │◀─── AUDIO_DATA ───────│                          │
  │◀─── AUDIO_DATA ───────│                          │
  │◀─── AUDIO_END ────────│                          │
  │                       │                          │
```

## 10. 潜在问题

### 10.1 Listen 消息丢失

**问题**: 设备发送音频但服务器未收到 listen 消息

**原因**: `SendStartListening` 仅在特定条件下调用

**修复**: 始终在进入 Listening 状态时发送

### 10.2 JSON 解析崩溃

**问题**: 未以 null 结尾的字符串导致崩溃

**修复**: 复制为 std::string 再解析

```cpp
std::string json_str(data, len);  // 保证 null 结尾
cJSON* root = cJSON_Parse(json_str.c_str());
```

### 10.3 PONG 阻塞

**问题**: 同步发送 PONG 阻塞接收任务

**修复**: 使用异步队列和独立任务

---

*文档版本: 1.0*
