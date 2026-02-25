# 4G 音频控制面/数据面分离 — 任务清单

> 创建时间: 2026-02-25
> 状态: ✅ Phase 0 + Phase 1 已完成，Phase 2 待评估
> 目标: 解决 4G 音频三大遗留问题（丢包爆音、打断延迟、NAT 超时断连）
> 参考文档: [MODULE_AUDIO.md 第14节](MODULE_AUDIO.md)
> 改动量: ~100 行代码，不新增线程，不增加内存（+2 字节）

## 架构概览

```
┌─── 数据面（设备接收）──────────────────────────────┐
│  AUDIO_DATA: WebSocket binary，服务端→设备            │
│  序列号: BinaryProtocol3.reserved (8-bit 循环)        │
│  丢包恢复: Opus PLC (NULL packet decode)             │
└───────────────────────────────────────────────────┘

┌─── 控制面（被动/异步）─────────────────────────────┐
│  Ping: 服务端发起 → 设备 Pong 自动回复               │
│  心跳: 基于 RX 活跃度判断，数据断流时才补发            │
│  超时: 15秒无数据 → 合成 AUDIO_END，防止卡死          │
│  打断: FlushPipeline 原子清空管线 + I2S DMA 重置     │
└───────────────────────────────────────────────────┘
```

---

## Phase 0: 设备端核心改动（P0，不依赖服务端）

### 0.1 智能心跳保活

| 字段 | 值 |
|------|-----|
| **序号** | 0.1 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 替换 OnHeartbeatTimer() 中的粗暴跳过逻辑，改为基于 RX 活跃度的智能心跳：有数据不发 Ping，数据断流时补发，15 秒无数据合成 AUDIO_END 防卡死 |
| **参考文档** | [MODULE_AUDIO.md §14.5.2](MODULE_AUDIO.md) |
| **修改文件** | `main/protocols/websocket_protocol.cc` |

**改动要点:**

替换 `OnHeartbeatTimer()`（当前 websocket_protocol.cc:55-65）：

```cpp
void WebsocketProtocol::OnHeartbeatTimer() {
    auto now = std::chrono::steady_clock::now();
    auto since_last_rx = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_incoming_time_).count();

    if (audio_streaming_) {
        if (since_last_rx < HEARTBEAT_INTERVAL_MS / 1000) {
            return;  // 近期有数据，NAT 存活
        }
        if (since_last_rx < 15) {
            // 数据断流 8-15 秒：补发 Ping 保活
            ESP_LOGW(TAG, "No RX for %llds during streaming, sending keepalive ping",
                     (long long)since_last_rx);
            if (websocket_ && websocket_->IsConnected()) {
                websocket_->Ping();
            }
        } else {
            // >15 秒无数据：AUDIO_END 丢失，强制结束
            ESP_LOGE(TAG, "No RX for %llds, forcing stream end", (long long)since_last_rx);
            audio_streaming_ = false;
            if (on_incoming_json_) {
                cJSON* root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "type", "tts");
                cJSON_AddStringToObject(root, "state", "stop");
                on_incoming_json_(root);
                cJSON_Delete(root);
            }
        }
        return;
    }

    // 非音频期间：正常心跳
    if (websocket_ && websocket_->IsConnected()) {
        websocket_->Ping();
    }
}
```

**验证:**
```bash
# 长时间播放 TTS，观察心跳行为
timeout 120 cat /dev/ttyACM0 | grep -E "keepalive ping|forcing stream end|heartbeat"
```

---

### 0.2 FlushPipeline 快速打断

| 字段 | 值 |
|------|-----|
| **序号** | 0.2 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 新增 FlushPipeline() 接口，原子级清空 decode_queue + playback_queue + Opus 解码器状态 + 播放控制器状态，使打断延迟从 1-2 秒降至 <50ms |
| **参考文档** | [MODULE_AUDIO.md §14.4](MODULE_AUDIO.md) |
| **修改文件** | `main/audio/audio_service.h`, `main/audio/audio_service.cc`, `main/application.cc` |

**改动要点:**

1) `audio_service.h` — 新增声明：

```cpp
void FlushPipeline();
```

2) `audio_service.cc` — 实现：

```cpp
void AudioService::FlushPipeline() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    size_t d = audio_decode_queue_.size();
    size_t p = audio_playback_queue_.size();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    opus_decoder_->ResetState();
    playback_controller_.Reset();
    ESP_LOGI(TAG, "Pipeline flushed: decode=%d, playback=%d", (int)d, (int)p);
    audio_queue_cv_.notify_all();
}
```

3) `application.cc` — abort 路径集成：

```cpp
// AbortSpeaking 或唤醒词打断时
protocol_->SendAbortSpeaking(reason);
audio_service_.FlushPipeline();
protocol_->SetAudioStreaming(false);
SetDeviceState(kDeviceStateListening);
```

**验证:**
```bash
# 播放 TTS 中按键打断，观察日志
timeout 30 cat /dev/ttyACM0 | grep -E "Pipeline flushed|AbortSpeaking"
```

---

### 0.3 I2S DMA 快速清空

| 字段 | 值 |
|------|-----|
| **序号** | 0.3 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 新增 FlushOutput() 接口，利用 i2s_channel_disable/enable 重置 DMA 描述符链，立即停止扬声器输出 |
| **参考文档** | [MODULE_AUDIO.md §14.4.3](MODULE_AUDIO.md), [ESP-IDF I2S API](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/api-reference/peripherals/i2s.html) |
| **修改文件** | `main/audio/codecs/box_audio_codec.h`, `main/audio/codecs/box_audio_codec.cc` |

**改动要点:**

```cpp
// box_audio_codec.h — 新增
void FlushOutput();

// box_audio_codec.cc — 实现
void BoxAudioCodec::FlushOutput() {
    if (tx_handle_) {
        i2s_channel_disable(tx_handle_);
        i2s_channel_enable(tx_handle_);  // 重置 DMA，< 1ms
    }
}
```

在 0.2 的 abort 路径中调用：`codec_->FlushOutput();`

**验证:** 打断时扬声器是否立即静音（无残余音频尾巴）。

---

### 0.4 低水位 PLC 兜底

| 字段 | 值 |
|------|-----|
| **序号** | 0.4 |
| **状态** | [x] 已完成 |
| **优先级** | P0 |
| **任务目标** | 新增 Opus PLC 解码方法，在 buffer underrun 时主动生成补偿帧，消除丢包爆音。不依赖序列号，纯设备端改动 |
| **参考文档** | [MODULE_AUDIO.md §14.3.3-14.3.4](MODULE_AUDIO.md), [RFC 6716 §4.3](https://tools.ietf.org/html/rfc6716#section-4.3) |
| **修改文件** | `managed_components/78__esp-opus-encoder/include/opus_decoder.h`, `managed_components/78__esp-opus-encoder/opus_decoder.cc`, `main/audio/audio_service.cc`, `main/audio/audio_service.h` |

**改动要点:**

1) `opus_decoder.h` — 新增声明：

```cpp
bool DecodePLC(std::vector<int16_t>& pcm);
```

2) `opus_decoder.cc` — 实现：

```cpp
bool OpusDecoderWrapper::DecodePLC(std::vector<int16_t>& pcm) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audio_dec_ == nullptr) return false;
    pcm.resize(frame_size_);
    auto ret = opus_decode(audio_dec_, NULL, 0, pcm.data(), pcm.size(), 0);
    if (ret < 0) return false;
    pcm.resize(ret);
    return true;
}
```

3) `audio_service.h` — 统计字段新增：

```cpp
struct DebugStatistics {
    // ...已有字段...
    uint32_t plc_count = 0;
};
```

4) `audio_service.cc` — OpusCodecTask() 末尾追加低水位 PLC 逻辑：

```cpp
// decode_queue 空 + 正在播放 + 未收到 AUDIO_END + playback 快空 → 网络抖动
if (audio_decode_queue_.empty() &&
    playback_controller_.GetState() == PlaybackController::PLAYING &&
    !playback_controller_.IsAudioEndReceived() &&
    audio_playback_queue_.size() < 2) {
    lock.unlock();
    auto task = std::make_unique<AudioTask>();
    task->type = kAudioTaskTypeDecodeToPlaybackQueue;
    if (opus_decoder_->DecodePLC(task->pcm)) {
        lock.lock();
        audio_playback_queue_.push_back(std::move(task));
        audio_queue_cv_.notify_all();
        debug_statistics_.plc_count++;
    } else {
        lock.lock();
    }
}
```

5) `playback_controller.h` — 新增访问器：

```cpp
bool IsAudioEndReceived() const { return audio_end_received_; }
```

**验证:**
```bash
timeout 60 cat /dev/ttyACM0 | grep "plc_count"
```

---

## Phase 1: 协议扩展（P1-P2，需服务端配合）

### 1.1 序列号 + 精确丢包检测

| 字段 | 值 |
|------|-----|
| **序号** | 1.1 |
| **状态** | [x] 已完成 |
| **优先级** | P1 |
| **任务目标** | 利用 BinaryProtocol3.reserved 字段传递 8-bit 循环序列号，设备端检测间隙插入 PLC 帧，实现精确丢包恢复 |
| **参考文档** | [MODULE_AUDIO.md §14.3.1-14.3.2](MODULE_AUDIO.md) |
| **修改文件（设备端）** | `main/protocols/websocket_protocol.cc`, `main/protocols/websocket_protocol.h` |
| **修改文件（服务端）** | `xiaozhi-server/src/xiaozhi_server/api/websocket.py` |

**设备端改动要点:**

1) `websocket_protocol.h` — 新增状态字段：

```cpp
uint8_t last_seq_ = 0;
bool last_seq_valid_ = false;
```

2) `websocket_protocol.cc` — AUDIO_DATA (0x11) 处理中，正常帧推入前插入丢包检测：

```cpp
if (msg_type == 0x11) {
    uint8_t seq = bp3->sequence;  // 原 reserved 字段

    // 序列号间隙检测（seq=0 可能是旧服务端，跳过检测）
    if (last_seq_valid_ && seq != 0) {
        uint8_t expected = (last_seq_ + 1) & 0xFF;
        int lost = (seq - expected) & 0xFF;
        if (lost > 0 && lost < 10) {
            ESP_LOGW(TAG, "Packet loss: expected=%d got=%d lost=%d", expected, seq, lost);
            for (int i = 0; i < lost; i++) {
                auto plc = std::make_unique<AudioStreamPacket>();
                plc->sample_rate = server_sample_rate_;
                plc->frame_duration = server_frame_duration_;
                plc->payload.clear();  // 空 payload = PLC 标记
                on_incoming_audio_(std::move(plc));
            }
        }
    }
    last_seq_ = seq;
    last_seq_valid_ = (seq != 0);

    // ... 原有正常帧处理逻辑不变 ...
}
```

3) AUDIO_START (0x10) 处理中，重置序列号状态：

```cpp
} else if (msg_type == 0x10) {
    last_seq_ = 0;
    last_seq_valid_ = false;
    // ... 原有逻辑不变 ...
}
```

**服务端改动要点:**

`xiaozhi-server/src/xiaozhi_server/api/websocket.py` — 发送 AUDIO_DATA 时填充 sequence 字段：

```python
# 每次 AUDIO_START 时重置
self._audio_seq = 0

# 发送 AUDIO_DATA 时递增
def _pack_audio_frame(self, opus_data: bytes) -> bytes:
    self._audio_seq = (self._audio_seq + 1) & 0xFF
    header = struct.pack('!BBH', 0x11, self._audio_seq, len(opus_data))
    return header + opus_data
```

**设备端解码分支改动** (`audio_service.cc` OpusCodecTask)：

```cpp
bool decoded = false;
if (packet->payload.empty()) {
    decoded = opus_decoder_->DecodePLC(task->pcm);
    if (decoded) debug_statistics_.plc_count++;
} else {
    decoded = opus_decoder_->Decode(std::move(packet->payload), task->pcm);
}
```

**验证:**
```bash
# 设备端丢包日志
timeout 60 cat /dev/ttyACM0 | grep "Packet loss"
# 对比服务端/设备端帧数
journalctl -u xiaozhi-server | grep "Sent .* Opus frames"
timeout 60 cat /dev/ttyACM0 | grep "AUDIO RX STATS"
```

---

### 1.2 服务端启用 WebSocket Ping

| 字段 | 值 |
|------|-----|
| **序号** | 1.2 |
| **状态** | [x] 已完成 |
| **优先级** | P2 |
| **任务目标** | 让服务端作为 Ping 发起方（每 10 秒），设备端 ML307 已有自动 Pong 机制，从根本上解决 NAT 保活问题 |
| **参考文档** | [MODULE_AUDIO.md §14.5.3](MODULE_AUDIO.md) |
| **修改文件** | `/opt/xiaozhi-eye/xiaozhi-server` systemd 服务配置, `eye/xiaozhi-server/src/xiaozhi_server/main.py`(如 uvicorn 参数在代码中) |

**改动要点:**

服务端 uvicorn 启动参数：

```bash
# 当前（Ping 禁用）:
uvicorn ... --ws-ping-interval 0 --ws-ping-timeout 300

# 改为（每 10 秒 Ping，30 秒超时）:
uvicorn ... --ws-ping-interval 10 --ws-ping-timeout 30
```

systemd 服务文件 `/etc/systemd/system/xiaozhi-server.service`：

```ini
ExecStart=/.../uvicorn xiaozhi_server.main:app --host 0.0.0.0 --port 6100 --ws-ping-interval 10 --ws-ping-timeout 30
```

**前置条件:** 需先完成 0.1（智能心跳），确保设备端不会因 Pong 发送与 URC 竞争导致问题。ML307 的 Pong 回复（`web_socket.cc:391`）是 `std::thread(...).detach()`，需验证在音频流期间是否稳定。

**验证:**
```bash
# 服务端日志确认 Ping 生效
journalctl -u xiaozhi-server | grep -i ping
# 设备端确认 Pong 回复
timeout 60 cat /dev/ttyACM0 | grep -iE "pong|ping"
```

---

## Phase 2: 长期优化（P3-P4）

### 2.1 服务端 In-band FEC

| 字段 | 值 |
|------|-----|
| **序号** | 2.1 |
| **状态** | [ ] 待评估 |
| **优先级** | P4 |
| **任务目标** | 服务端 TTS Opus 编码启用 In-band FEC，设备端利用 FEC 数据恢复丢失帧，实现最高质量的丢包恢复 |
| **参考文档** | [MODULE_AUDIO.md §14.3](MODULE_AUDIO.md), [RFC 6716 FEC](https://tools.ietf.org/html/rfc6716) |
| **修改文件（服务端）** | `xiaozhi-server/src/xiaozhi_server/services/tts_service.py` 或 `opus_ogg.py` |
| **修改文件（设备端）** | `managed_components/78__esp-opus-encoder/opus_decoder.cc` |

**要点:**
- 服务端编码时启用 `OPUS_SET_INBAND_FEC(1)` + `OPUS_SET_PACKET_LOSS_PERC(20)`
- 设备端解码时，检测到丢包后用下一帧的 FEC 数据恢复：`opus_decode(dec, next_data, next_len, pcm, size, 1)`
- 需要缓冲一帧延迟（60ms），在 4G 1800ms 预缓冲下可忽略
- FEC 增加约 20-30% 码率，语音场景（~16kbps）影响很小

**注意:** 此任务依赖服务端 TTS 输出的 Opus 流是否可控（阿里云 CosyVoice 直接输出 Opus，可能无法控制 FEC 参数）。需先评估 TTS 服务的 Opus 编码配置能力。

---

## 依赖关系

```
Phase 0（设备端独立）         Phase 1（需服务端配合）      Phase 2（长期）
┌─────┐  ┌─────┐             ┌─────┐                    ┌─────┐
│ 0.1 │  │ 0.2 │             │ 1.1 │                    │ 2.1 │
│心跳 │  │Flush│             │序列号│                    │ FEC │
└──┬──┘  └──┬──┘             └──┬──┘                    └──┬──┘
   │        │                   │                          │
   │     ┌──┴──┐                │                          │
   │     │ 0.3 │             ┌──┴──┐                       │
   │     │I2S  │             │ 1.2 │                       │
   │     └─────┘             │Ping │                       │
   │                         └─────┘                       │
   │     ┌─────┐                                           │
   └────▶│ 0.4 │◀──────────────────────────────────────────┘
         │ PLC │  (0.4 是 1.1 和 2.1 的基础)
         └─────┘

Phase 0 内部无依赖，四个任务可并行实施
Phase 1.1 依赖 0.4（PLC 解码方法）
Phase 1.2 建议在 0.1 验证后再实施
Phase 2.1 依赖 0.4 + 1.1
```

---

## 文件变更总表

| 文件 | 操作 | 涉及任务 | 改动行数 |
|------|------|----------|----------|
| `main/protocols/websocket_protocol.cc` | 修改 | 0.1, 1.1 | ~50 |
| `main/protocols/websocket_protocol.h` | 修改 | 1.1 | ~3 |
| `main/audio/audio_service.cc` | 修改 | 0.2, 0.4, 1.1 | ~30 |
| `main/audio/audio_service.h` | 修改 | 0.2, 0.4 | ~3 |
| `main/audio/playback_controller.h` | 修改 | 0.4 | ~1 |
| `main/audio/codecs/box_audio_codec.h` | 修改 | 0.3 | ~1 |
| `main/audio/codecs/box_audio_codec.cc` | 修改 | 0.3 | ~6 |
| `managed_components/78__esp-opus-encoder/include/opus_decoder.h` | 修改 | 0.4 | ~1 |
| `managed_components/78__esp-opus-encoder/opus_decoder.cc` | 修改 | 0.4 | ~10 |
| `main/application.cc` | 修改 | 0.2 | ~5 |
| `xiaozhi-server/.../api/websocket.py` | 修改 | 1.1 | ~10 |
| `xiaozhi-server systemd service` | 修改 | 1.2 | ~1 |
| **新增文件** | **无** | - | - |
| **删除文件** | **无** | - | - |
| **总计** | 10 文件修改 | - | **~120 行** |

---

*文档版本: 1.1 - 2026-02-25 (Phase 0 + Phase 1 全部完成)*
