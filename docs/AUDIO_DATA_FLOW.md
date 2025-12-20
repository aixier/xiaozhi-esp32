# 语音数据流完整技术文档

> 文档状态: 完成
> 最后更新: 2025-12-20

## 第一部分：系统架构概览

### 1.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              设备端 (ESP32-S3)                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌──────────┐    ┌──────────────┐    ┌──────────────┐    ┌─────────────┐  │
│   │  麦克风   │───▶│ AudioCodec   │───▶│ OpusEncoder  │───▶│  WebSocket  │  │
│   │          │    │  (I2S/ADC)   │    │  (16kHz/60ms)│    │   Client    │  │
│   └──────────┘    └──────────────┘    └──────────────┘    └──────┬──────┘  │
│                                                                   │         │
│   ┌──────────┐    ┌──────────────┐    ┌──────────────┐           │         │
│   │  扬声器   │◀───│ AudioCodec   │◀───│ OpusDecoder  │◀──────────┘         │
│   │          │    │  (I2S/DAC)   │    │  (16kHz/60ms)│                      │
│   └──────────┘    └──────────────┘    └──────────────┘                      │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        │ WebSocket (ws://)
                                        │ BinaryProtocol3
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              云端 (xiaozhi-server)                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                 │
│   │   FastAPI    │───▶│  ASR 服务    │───▶│  LLM 服务    │                 │
│   │  WebSocket   │    │  (Gummy)     │    │  (Qwen)      │                 │
│   └──────────────┘    └──────────────┘    └──────┬───────┘                 │
│          │                                       │                          │
│          │◀──────────────────────────────────────┼──────────────────────┐   │
│          │                                       ▼                      │   │
│          │                              ┌──────────────┐                │   │
│          │                              │  TTS 服务    │────────────────┘   │
│          │                              │  (CosyVoice) │                    │
│          │                              └──────────────┘                    │
│          ▼                                                                  │
│   ┌──────────────┐                                                          │
│   │   数据库     │  Sessions, Conversations, Devices                        │
│   │  PostgreSQL  │                                                          │
│   └──────────────┘                                                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 核心组件说明

| 组件 | 位置 | 职责 |
|------|------|------|
| AudioService | 设备端 | 音频采集、播放、Opus 编解码 |
| WebsocketProtocol | 设备端 | WebSocket 通信、二进制协议 |
| websocket.py | 云端 | WebSocket 端点、消息路由 |
| asr_service.py | 云端 | 语音识别 (DashScope Gummy) |
| llm_service.py | 云端 | 大语言模型 (Qwen) |
| tts_service.py | 云端 | 语音合成 (CosyVoice) |

### 1.3 通信协议概述

| 层级 | 协议 | 说明 |
|------|------|------|
| 传输层 | WebSocket | 全双工通信 |
| 应用层 | BinaryProtocol3 | 二进制音频帧 |
| 应用层 | JSON | 控制消息 (hello, listen, abort) |
| 音频编码 | Opus | 16kHz, mono, 60ms 帧 |

---

## 第二部分：完整时序图

### 2.1 端到端时序图 (从唤醒到响应完成)

```
┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐
│  用户     │      │  设备     │      │ 云端WS   │      │  ASR     │      │  LLM     │      │  TTS     │
└────┬─────┘      └────┬─────┘      └────┬─────┘      └────┬─────┘      └────┬─────┘      └────┬─────┘
     │                 │                 │                 │                 │                 │
     │ 唤醒词/按钮      │                 │                 │                 │                 │
     ├────────────────▶│                 │                 │                 │                 │
     │                 │                 │                 │                 │                 │
     │                 │ WS Connect      │                 │                 │                 │
     │                 ├────────────────▶│                 │                 │                 │
     │                 │                 │                 │                 │                 │
     │                 │ Client Hello    │                 │                 │                 │
     │                 │ {type:hello,    │                 │                 │                 │
     │                 │  audio_params}  │                 │                 │                 │
     │                 ├────────────────▶│                 │                 │                 │
     │                 │                 │                 │                 │                 │
     │                 │ Server Hello    │                 │                 │                 │
     │                 │ {session_id,    │                 │                 │                 │
     │                 │  audio_params}  │                 │                 │                 │
     │                 │◀────────────────┤                 │                 │                 │
     │                 │                 │                 │                 │                 │
     │ 说话             │                 │                 │                 │                 │
     ├────────────────▶│                 │                 │                 │                 │
     │                 │ {type:listen,   │                 │                 │                 │
     │                 │  state:start}   │                 │                 │                 │
     │                 ├────────────────▶│                 │                 │                 │
     │                 │                 │ Create Session  │                 │                 │
     │                 │                 ├────────────────▶│                 │                 │
     │                 │                 │                 │                 │                 │
     │                 │ Binary Audio    │                 │                 │                 │
     │                 │ [BinaryProto3]  │                 │                 │                 │
     │                 ├────────────────▶│ send_audio()    │                 │                 │
     │                 │     ....        ├────────────────▶│                 │                 │
     │                 │ (多帧音频)       │                 │                 │                 │
     │                 ├────────────────▶│                 │                 │                 │
     │                 │                 │                 │ Partial Result  │                 │
     │                 │                 │◀────────────────┤                 │                 │
     │                 │ {TEXT_ASR:      │                 │                 │                 │
     │                 │  "你好..."}     │                 │                 │                 │
     │                 │◀────────────────┤                 │                 │                 │
     │                 │                 │                 │                 │                 │
     │ 停止说话         │                 │                 │                 │                 │
     ├────────────────▶│                 │                 │                 │                 │
     │                 │ {type:listen,   │                 │                 │                 │
     │                 │  state:stop}    │                 │                 │                 │
     │                 ├────────────────▶│ stop()          │                 │                 │
     │                 │                 ├────────────────▶│                 │                 │
     │                 │                 │                 │ Final Result    │                 │
     │                 │                 │◀────────────────┤ "你好小智"       │                 │
     │                 │                 │                 │                 │                 │
     │                 │                 │ chat_stream()   │                 │                 │
     │                 │                 ├─────────────────────────────────▶│                 │
     │                 │                 │                 │                 │                 │
     │                 │                 │                 │ Stream Chunk    │                 │
     │                 │                 │◀────────────────────────────────┬┤                 │
     │                 │ {TEXT_LLM:      │                 │                 │                 │
     │                 │  "你好！"}      │                 │                 │                 │
     │                 │◀────────────────┤                 │                 │                 │
     │                 │                 │ synthesize()    │                 │                 │
     │                 │                 ├─────────────────────────────────────────────────▶│
     │                 │                 │                 │                 │  Audio Chunk   │
     │                 │                 │◀────────────────────────────────────────────────┬┤
     │                 │ Binary Audio    │                 │                 │                 │
     │                 │ [AUDIO_DATA]    │                 │                 │                 │
     │                 │◀────────────────┤                 │                 │                 │
     │                 │     ....        │                 │                 │                 │
     │                 │ (多帧音频)       │                 │                 │                 │
     │                 │◀────────────────┤                 │                 │                 │
     │                 │                 │                 │                 │                 │
     │                 │ [AUDIO_END]     │                 │                 │                 │
     │                 │◀────────────────┤                 │                 │                 │
     │ 听到回复         │                 │                 │                 │                 │
     │◀────────────────┤                 │                 │                 │                 │
     │                 │                 │                 │                 │                 │
```

### 2.2 关键阶段时序

#### 阶段 1: 唤醒 → 建立连接

```
设备                                    云端
  │                                      │
  │──── WS Connect (token in URL) ──────▶│
  │                                      │ authenticate_websocket()
  │                                      │ decode_token()
  │                                      │ get Device from DB
  │                                      │
  │◀─── WS Accept ──────────────────────│
  │                                      │
  │──── Client Hello JSON ──────────────▶│
  │     {                                │
  │       "type": "hello",               │
  │       "version": 3,                  │
  │       "audio_params": {              │
  │         "format": "opus",            │
  │         "sample_rate": 16000,        │
  │         "channels": 1,               │
  │         "frame_duration": 60         │
  │       }                              │
  │     }                                │
  │                                      │ Create Session in DB
  │                                      │ Update device status = online
  │                                      │
  │◀─── Server Hello JSON ──────────────│
  │     {                                │
  │       "type": "hello",               │
  │       "transport": "websocket",      │
  │       "session_id": "xxx",           │
  │       "audio_params": {...}          │
  │     }                                │
  │                                      │
```

#### 阶段 2: 录音 → 音频上传

```
设备                                    云端
  │                                      │
  │──── Listen Start JSON ──────────────▶│
  │     {"type":"listen","state":"start"}│
  │                                      │ handle_audio_start()
  │                                      │ asr_service.create_session()
  │                                      │ Connect to DashScope ASR
  │                                      │
  │ AudioInputTask                       │
  │   ↓ PCM 16kHz                        │
  │ OpusCodecTask                        │
  │   ↓ Opus encoded                     │
  │ WebsocketProtocol::SendAudio()       │
  │                                      │
  │──── BinaryProtocol3 ────────────────▶│ handle_audio_data()
  │     [type=0][reserved][size][Opus]   │ asr_session.send_audio()
  │                                      │
  │──── BinaryProtocol3 ────────────────▶│
  │     .....(持续发送音频帧).....        │
  │                                      │
  │◀─── TEXT_ASR (partial) ─────────────│ on_partial callback
  │                                      │
  │──── Listen Stop JSON ───────────────▶│
  │     {"type":"listen","state":"stop"} │ handle_audio_end()
  │                                      │ asr_session.stop()
  │                                      │
  │◀─── TEXT_ASR (final) ───────────────│ on_final callback
  │                                      │
```

#### 阶段 3: ASR → LLM → TTS

```
云端 WebSocket          ASR (DashScope)      LLM (Qwen)           TTS (CosyVoice)
      │                      │                   │                      │
      │                      │                   │                      │
      │ on_final("你好")     │                   │                      │
      │◀─────────────────────│                   │                      │
      │                      │                   │                      │
      │ process_user_input() │                   │                      │
      ├──────────────────────────────────────────▶ chat_stream()        │
      │                      │                   │                      │
      │                      │    Stream chunk   │                      │
      │◀─────────────────────────────────────────┤                      │
      │ send TEXT_LLM        │                   │                      │
      │                      │                   │                      │
      │ synthesize_streaming_text()              │                      │
      ├──────────────────────────────────────────────────────────────────▶
      │                      │                   │     Audio chunk      │
      │◀─────────────────────────────────────────────────────────────────┤
      │ send AUDIO_DATA      │                   │                      │
      │                      │                   │                      │
      │      .....(流式处理，边生成边合成边发送).....                     │
      │                      │                   │                      │
      │ send AUDIO_END       │                   │                      │
      │                      │                   │                      │
```

#### 阶段 4: 音频下发 → 播放

```
云端                                    设备
  │                                      │
  │──── BinaryProtocol3 ────────────────▶│ OnData callback
  │     [type=0][reserved][size][Opus]   │ Parse BinaryProtocol3
  │                                      │ Create AudioStreamPacket
  │                                      │
  │──── BinaryProtocol3 ────────────────▶│ on_incoming_audio_()
  │     .....(多帧音频).....              │   ↓
  │                                      │ audio_decode_queue_
  │                                      │   ↓
  │                                      │ OpusCodecTask (decode)
  │                                      │   ↓ PCM 16kHz
  │                                      │ audio_playback_queue_
  │                                      │   ↓
  │                                      │ AudioOutputTask
  │                                      │   ↓
  │                                      │ AudioCodec (I2S/DAC)
  │                                      │   ↓
  │                                      │ 扬声器播放
  │                                      │
  │──── AUDIO_END ──────────────────────▶│
  │     [type=2]                         │
  │                                      │
```

---

## 第三部分：设备端实现

### 3.1 音频采集模块

**文件**: `main/audio/audio_service.cc`

```cpp
// AudioInputTask (line 207-274)
void AudioService::AudioInputTask() {
    while (running_) {
        // 1. 从 codec 读取 PCM 数据
        codec_->Read(audio_buffer, AUDIO_FRAME_SIZE);

        // 2. 送入 AFE (音频前端) 处理
        afe_->Feed(audio_buffer);

        // 3. 检测唤醒词
        if (wake_word_detected) {
            on_wake_word_detected_();
        }

        // 4. 发送到编码队列
        xQueueSend(audio_encode_queue_, &packet, portMAX_DELAY);
    }
}
```

**音频参数**:
- 采样率: 16000 Hz
- 通道数: 1 (Mono)
- 位深: 16-bit
- 帧大小: 960 samples (60ms)

### 3.2 音频编码 (Opus)

**文件**: `main/audio/audio_service.cc` (line 311-388)

```cpp
// OpusCodecTask
void AudioService::OpusCodecTask() {
    while (running_) {
        // 编码: PCM → Opus
        if (xQueueReceive(audio_encode_queue_, &packet, 0) == pdTRUE) {
            int encoded_size = opus_encode(
                encoder_,
                (opus_int16*)packet.data(),
                OPUS_FRAME_SIZE,    // 960 samples
                encoded_buffer,
                MAX_ENCODED_SIZE
            );

            // 发送到 WebSocket 发送队列
            xQueueSend(audio_send_queue_, &encoded_packet, portMAX_DELAY);
        }

        // 解码: Opus → PCM
        if (xQueueReceive(audio_decode_queue_, &packet, 0) == pdTRUE) {
            int decoded_size = opus_decode(
                decoder_,
                packet.data(),
                packet.size(),
                (opus_int16*)decoded_buffer,
                OPUS_FRAME_SIZE,
                0
            );

            // 发送到播放队列
            xQueueSend(audio_playback_queue_, &decoded_packet, portMAX_DELAY);
        }
    }
}
```

**Opus 参数**:
- 帧时长: 60ms (`OPUS_FRAME_DURATION_MS`)
- 帧大小: 960 samples (16000 * 0.06)
- 比特率: 自适应

### 3.3 WebSocket 协议层

**文件**: `main/protocols/websocket_protocol.cc`

#### 连接建立 (line 82-225)

```cpp
bool WebsocketProtocol::OpenAudioChannel() {
    // 1. 创建 WebSocket 连接
    websocket_ = network->CreateWebSocket(1);
    websocket_->SetHeader("Authorization", "Bearer " + token);
    websocket_->SetHeader("Protocol-Version", std::to_string(version_));
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid());

    // 2. 连接服务器
    websocket_->Connect(url);

    // 3. 发送 Client Hello
    SendText(GetHelloMessage());  // JSON

    // 4. 等待 Server Hello
    xEventGroupWaitBits(event_group_handle_, SERVER_HELLO_EVENT, ...);
}
```

#### 音频发送 (line 28-58)

```cpp
bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    // BinaryProtocol3 格式
    std::string serialized;
    serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());

    auto bp3 = (BinaryProtocol3*)serialized.data();
    bp3->type = 0;                              // 0 = Audio
    bp3->reserved = 0;
    bp3->payload_size = htons(packet->payload.size());  // Big-endian
    memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

    return websocket_->Send(serialized.data(), serialized.size(), true);
}
```

#### 音频接收 (line 114-150)

```cpp
websocket_->OnData([this](const char* data, size_t len, bool binary) {
    if (binary && version_ == 3) {
        BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
        bp3->payload_size = ntohs(bp3->payload_size);  // Network → Host

        on_incoming_audio_(std::make_unique<AudioStreamPacket>({
            .sample_rate = server_sample_rate_,
            .frame_duration = server_frame_duration_,
            .payload = std::vector<uint8_t>(
                bp3->payload,
                bp3->payload + bp3->payload_size
            )
        }));
    }
});
```

### 3.4 音频播放模块

**文件**: `main/audio/audio_service.cc` (line 276-309)

```cpp
// AudioOutputTask
void AudioService::AudioOutputTask() {
    while (running_) {
        // 从播放队列获取 PCM 数据
        if (xQueueReceive(audio_playback_queue_, &packet, portMAX_DELAY) == pdTRUE) {
            // 写入 codec (I2S/DAC)
            codec_->Write(packet.data(), packet.size());
        }
    }
}
```

---

## 第四部分：云端实现

### 4.1 WebSocket 服务

**文件**: `xiaozhi-server/src/xiaozhi_server/api/websocket.py`

#### 连接处理 (line 283-461)

```python
@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()

    # 1. 认证
    device = await authenticate_websocket(websocket)

    # 2. 建立连接
    conn = await manager.connect(websocket, device)

    # 3. 等待设备 Hello
    hello_data = await websocket.receive_text()
    device_hello = json.loads(hello_data)

    # 4. 发送服务器 Hello
    await websocket.send_text(json.dumps({
        "type": "hello",
        "transport": "websocket",
        "session_id": conn.session_id,
        "audio_params": {...}
    }))

    # 5. 主消息循环
    while True:
        message = await websocket.receive()
        if "bytes" in message:
            # 解析 BinaryProtocol3
            msg_type, reserved, payload_len = struct.unpack(">BBH", data[:4])
            payload = data[4:4 + payload_len]

            if msg_type == 0:  # Audio
                await handle_audio_data(conn, payload)
```

#### 消息类型定义

```python
class MessageType(IntEnum):
    AUDIO_DATA = 0      # 音频数据
    AUDIO_START = 1     # 音频开始
    AUDIO_END = 2       # 音频结束
    TEXT_ASR = 3        # ASR 文本
    TEXT_LLM = 4        # LLM 文本
    ERROR = 5           # 错误消息
```

### 4.2 ASR 服务 (阿里云 Gummy)

**文件**: `xiaozhi-server/src/xiaozhi_server/services/asr_service.py`

#### 会话流程

```python
class ASRSession:
    async def start(self):
        # 1. 连接 DashScope WebSocket
        ws_url = "wss://dashscope.aliyuncs.com/api-ws/v1/inference"
        self.ws = await websockets.connect(ws_url, extra_headers={
            "Authorization": f"bearer {api_key}"
        })

        # 2. 发送 run-task
        await self.ws.send(json.dumps({
            "header": {
                "action": "run-task",
                "task_id": self.task_id,
                "streaming": "duplex"
            },
            "payload": {
                "model": "gummy-realtime-v1",
                "task_group": "audio",
                "task": "asr",
                "function": "recognition",
                "parameters": {
                    "sample_rate": 16000,
                    "format": "pcm"
                }
            }
        }))

        # 3. 等待 task-started
        # 4. 启动接收循环

    async def send_audio(self, audio_data: bytes):
        # 直接发送 PCM 音频 (需要先解码 Opus)
        await self.ws.send(audio_data)

    async def _receive_loop(self):
        while self._running:
            msg = await self.ws.recv()
            data = json.loads(msg)
            event = data["header"]["event"]

            if event == "result-generated":
                text = data["payload"]["output"]["sentence"]["text"]
                is_end = data["payload"]["output"]["sentence"]["sentence_end"]

                if is_end:
                    self.on_final(text)
                else:
                    self.on_partial(text)
```

### 4.3 LLM 服务 (通义千问)

**文件**: `xiaozhi-server/src/xiaozhi_server/services/llm_service.py`

#### 流式调用

```python
class LLMService:
    async def chat_stream(self, messages: list[dict]) -> AsyncGenerator[str, None]:
        # OpenAI 兼容 API
        url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"

        data = {
            "model": "qwen-plus",
            "messages": messages,
            "stream": True
        }

        async with httpx.AsyncClient() as client:
            async with client.stream("POST", url, json=data) as response:
                async for line in response.aiter_lines():
                    if line.startswith("data: "):
                        chunk = json.loads(line[6:])
                        content = chunk["choices"][0]["delta"].get("content", "")
                        if content:
                            yield content
```

### 4.4 TTS 服务 (CosyVoice)

**文件**: `xiaozhi-server/src/xiaozhi_server/services/tts_service.py`

#### 流式文本 → 流式音频

```python
class TTSService:
    async def synthesize_streaming_text(
        self,
        text_generator: AsyncGenerator[str, None]
    ) -> AsyncGenerator[bytes, None]:

        ws_url = "wss://dashscope.aliyuncs.com/api-ws/v1/inference"

        async with websockets.connect(ws_url) as ws:
            # 1. 发送 run-task
            # 重要：format 和 sample_rate 必须与设备端 Opus 解码器匹配！
            # 设备端只有 Opus 解码器，不支持 MP3/WAV
            await ws.send(json.dumps({
                "header": {"action": "run-task", ...},
                "payload": {
                    "model": "cosyvoice-v1",
                    "parameters": {
                        "voice": "longxiaochun",
                        "format": "opus",  # 必须是 opus (设备端只支持 Opus 解码)
                        "sample_rate": 16000  # 必须是 16000 (匹配设备期望)
                    }
                }
            }))

            # 2. 等待 task-started

            # 3. 流式发送文本 (来自 LLM)
            async for text_chunk in text_generator:
                await ws.send(json.dumps({
                    "header": {"action": "continue-task", ...},
                    "payload": {"input": {"text": text_chunk}}
                }))

            # 4. 发送 finish-task

            # 5. 接收音频数据
            while True:
                msg = await ws.recv()
                if isinstance(msg, bytes):
                    yield msg  # Opus 音频块
```

---

## 第五部分：数据格式规范

### 5.1 二进制协议格式

#### BinaryProtocol3 (当前使用)

```
┌─────────┬──────────┬────────────┬─────────────────────┐
│  type   │ reserved │ payload_sz │      payload        │
│ (1 byte)│ (1 byte) │ (2 bytes)  │    (N bytes)        │
│         │          │ Big-endian │                     │
└─────────┴──────────┴────────────┴─────────────────────┘

type:
  0 = Audio Data (Opus)
  1 = Audio Start
  2 = Audio End
```

**C 结构体定义**:
```cpp
struct BinaryProtocol3 {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;  // Network byte order (Big-endian)
    uint8_t payload[];
} __attribute__((packed));
```

**Python 解析**:
```python
msg_type, reserved, payload_len = struct.unpack(">BBH", data[:4])
payload = data[4:4 + payload_len]
```

#### BinaryProtocol2 (旧版本)

```
┌─────────┬─────────┬──────────┬───────────┬────────────┬──────────┐
│ version │  type   │ reserved │ timestamp │ payload_sz │ payload  │
│(2 bytes)│(2 bytes)│ (4 bytes)│ (4 bytes) │ (4 bytes)  │ (N bytes)│
└─────────┴─────────┴──────────┴───────────┴────────────┴──────────┘
```

### 5.2 JSON 消息格式

#### Client Hello

```json
{
    "type": "hello",
    "version": 3,
    "features": {
        "aec": true,
        "mcp": true
    },
    "transport": "websocket",
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

#### Server Hello

```json
{
    "type": "hello",
    "transport": "websocket",
    "session_id": "abc123...",
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

#### Listen 消息

```json
{
    "type": "listen",
    "mode": "auto",
    "state": "start"  // 或 "stop"
}
```

#### Abort 消息

```json
{
    "type": "abort"
}
```

### 5.3 音频参数规范

| 参数 | 设备端 (上行) | 云端 (下行) | 备注 |
|------|--------------|------------|------|
| 格式 | Opus | Opus | **必须匹配！** 设备只有 Opus 解码器 |
| 采样率 | 16000 Hz | 16000 Hz | **必须匹配！** 不支持自动重采样 |
| 通道数 | 1 (Mono) | 1 (Mono) | |
| 位深 | 16-bit | 16-bit | |
| 帧时长 | 60ms | 60ms | |
| 帧大小 | 960 samples | 960 samples | |

**重要配置 (xiaozhi-server/config.py)**:
```python
# TTS 配置 (必须与设备端 Opus 解码器匹配)
tts_format: str = "opus"      # 不能用 mp3/wav，设备无法解码
tts_sample_rate: int = 16000  # 不能用 22050/24000，设备期望 16000
```

**常见错误**:
- TTS 输出 MP3 格式 → 设备无法解码，无声音
- TTS 采样率 22050/24000 → 设备解码失败或播放异常

---

## 第六部分：关键代码索引

### 6.1 设备端文件清单

| 文件 | 行号 | 功能 |
|------|------|------|
| `main/audio/audio_service.cc` | 207-274 | AudioInputTask (音频采集) |
| `main/audio/audio_service.cc` | 276-309 | AudioOutputTask (音频播放) |
| `main/audio/audio_service.cc` | 311-388 | OpusCodecTask (编解码) |
| `main/protocols/websocket_protocol.cc` | 28-58 | SendAudio (发送音频) |
| `main/protocols/websocket_protocol.cc` | 82-225 | OpenAudioChannel (建立连接) |
| `main/protocols/websocket_protocol.cc` | 114-182 | OnData 回调 (接收处理) |
| `main/protocols/websocket_protocol.cc` | 227-250 | GetHelloMessage |
| `main/protocols/protocol.h` | 10-15 | AudioStreamPacket 定义 |
| `main/protocols/protocol.h` | 17-24 | BinaryProtocol2 定义 |
| `main/protocols/protocol.h` | 26-31 | BinaryProtocol3 定义 |

### 6.2 云端文件清单

| 文件 | 行号 | 功能 |
|------|------|------|
| `api/websocket.py` | 26-56 | DeviceConnection 类 |
| `api/websocket.py` | 58-111 | ConnectionManager 类 |
| `api/websocket.py` | 149-170 | handle_audio_start |
| `api/websocket.py` | 173-177 | handle_audio_data |
| `api/websocket.py` | 195-256 | process_user_input (LLM+TTS) |
| `api/websocket.py` | 283-461 | websocket_endpoint |
| `api/websocket.py` | 403-409 | BinaryProtocol3 解析 |
| `services/asr_service.py` | 17-176 | ASRSession 类 |
| `services/asr_service.py` | 42-97 | start() (连接 DashScope) |
| `services/asr_service.py` | 136-175 | _receive_loop() |
| `services/llm_service.py` | 60-105 | chat_stream() (流式 LLM) |
| `services/tts_service.py` | 51-137 | synthesize_stream() |
| `services/tts_service.py` | 139-243 | synthesize_streaming_text() |

---

## 附录：数据流队列架构

```
设备端 FreeRTOS 队列架构:

┌──────────────┐
│ AudioCodec   │──▶ audio_buffer
│ (I2S Read)   │
└──────────────┘
       │
       ▼
┌──────────────┐    ┌──────────────┐
│ AFE (音频    │───▶│ WakeWord     │
│  前端处理)   │    │  Detection   │
└──────────────┘    └──────────────┘
       │
       ▼
┌──────────────────┐
│ audio_encode_    │  ◀─── AudioInputTask
│     queue_       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ OpusCodecTask    │  Opus Encode
│ (Encoder)        │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ audio_send_      │  ◀─── Encoded Opus
│     queue_       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ WebSocket        │  ───▶ 云端
│ SendAudio()      │
└──────────────────┘


         │
         │ (从云端接收)
         ▼
┌──────────────────┐
│ WebSocket        │
│ OnData()         │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ audio_decode_    │  ◀─── Encoded Opus (from server)
│     queue_       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ OpusCodecTask    │  Opus Decode
│ (Decoder)        │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ audio_playback_  │  ◀─── Decoded PCM
│     queue_       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ AudioOutputTask  │
│ AudioCodec Write │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Speaker          │
│ (I2S/DAC)        │
└──────────────────┘
```
