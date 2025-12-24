# Audio 模块分析

## 1. 模块概述

AudioService 是音频子系统的核心，负责：
- 音频采集与播放
- Opus 编解码
- 唤醒词检测
- VAD 语音活动检测
- 预缓冲控制

## 2. 类结构

### 2.1 AudioService

```cpp
class AudioService {
public:
    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();

    // 状态查询
    bool IsVoiceDetected() const;
    bool IsIdle();
    bool IsWakeWordRunning() const;
    bool IsAudioProcessorRunning() const;

    // 功能控制
    void EnableWakeWordDetection(bool enable);
    void EnableVoiceProcessing(bool enable);
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);

    // 数据接口
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    void PlaySound(const std::string_view& sound);

    // 预缓冲
    void StartPrebuffering();
    void StopPrebuffering();
    void ResetDecoder();

private:
    // 编解码器
    AudioCodec* codec_;
    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    // 处理器
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<WakeWord> wake_word_;

    // 任务句柄
    TaskHandle_t audio_input_task_handle_;
    TaskHandle_t audio_output_task_handle_;
    TaskHandle_t opus_codec_task_handle_;

    // 队列
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_send_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_playback_queue_;
};
```

### 2.2 AudioStreamPacket

```cpp
struct AudioStreamPacket {
    int sample_rate = 0;        // 采样率 (16000)
    int frame_duration = 0;      // 帧时长 (60ms)
    uint32_t timestamp = 0;      // 时间戳
    std::vector<uint8_t> payload; // Opus 数据
};
```

### 2.3 AudioTask

```cpp
enum AudioTaskType {
    kAudioTaskTypeEncodeToSendQueue,    // 编码后发送
    kAudioTaskTypeEncodeToTestingQueue, // 编码后测试
    kAudioTaskTypeDecodeToPlaybackQueue // 解码后播放
};

struct AudioTask {
    AudioTaskType type;
    std::vector<int16_t> pcm;   // PCM 数据
    uint32_t timestamp;
};
```

## 3. 数据流架构

### 3.1 发送链路 (MIC → Server)

```
┌─────────┐   PCM    ┌───────────────┐   PCM    ┌──────────────┐
│   MIC   │─────────▶│ AudioProcessor │─────────▶│ EncodeQueue  │
└─────────┘  16kHz   │ (AFE/VAD)      │         └──────────────┘
                     └───────────────┘                  │
                                                        ▼
                     ┌─────────────┐   Opus    ┌──────────────┐
                     │  SendQueue  │◀──────────│ OpusEncoder  │
                     └─────────────┘           └──────────────┘
                            │
                            ▼
                     ┌─────────────┐
                     │  WebSocket  │───────▶ Server
                     └─────────────┘
```

### 3.2 接收链路 (Server → Speaker)

```
Server ───────▶ ┌─────────────┐
                │  WebSocket  │
                └─────────────┘
                       │
                       ▼ Opus
                ┌──────────────┐   Opus    ┌──────────────┐
                │ DecodeQueue  │──────────▶│ OpusDecoder  │
                └──────────────┘           └──────────────┘
                                                  │
                                                  ▼ PCM
                ┌─────────────┐   PCM     ┌──────────────┐
                │   Speaker   │◀──────────│PlaybackQueue │
                └─────────────┘           └──────────────┘
```

## 4. FreeRTOS 任务

### 4.1 AudioInputTask

```cpp
void AudioService::AudioInputTask() {
    std::vector<int16_t> input_buffer(AUDIO_INPUT_BUFFER_SIZE);

    while (!service_stopped_) {
        // 1. 从硬件读取音频
        if (!codec_->ReadAudioData(input_buffer)) {
            continue;
        }

        // 2. 更新输入时间 (用于电源管理)
        last_input_time_ = std::chrono::steady_clock::now();

        // 3. 预热处理 (跳过初始噪音)
        if (audio_input_need_warmup_) {
            audio_input_need_warmup_ = false;
            continue;
        }

        // 4. 唤醒词检测
        if (IsWakeWordRunning()) {
            wake_word_->Process(input_buffer);
        }

        // 5. 语音处理 (编码发送)
        if (IsAudioProcessorRunning()) {
            audio_processor_->Process(input_buffer);
            // 输出推送到编码队列
        }

        // 6. 音频测试模式
        if (IsAudioTestingRunning()) {
            // 录音后回放
        }
    }
}
```

### 4.2 AudioOutputTask

```cpp
void AudioService::AudioOutputTask() {
    std::vector<int16_t> output_buffer;

    while (!service_stopped_) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);

        // 1. 等待播放数据
        audio_queue_cv_.wait(lock, [this]() {
            return !audio_playback_queue_.empty() || service_stopped_;
        });

        if (service_stopped_) break;

        // 2. 预缓冲检查
        if (prebuffering_) {
            if (audio_playback_queue_.size() < PREBUFFER_FRAMES) {
                continue;  // 等待更多数据
            }
            prebuffering_ = false;
        }

        // 3. 取出播放任务
        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();

        // 4. 检查队列是否变空
        bool was_not_empty = (xEventGroupGetBits(event_group_) & AS_EVENT_PLAYBACK_NOT_EMPTY);
        if (audio_playback_queue_.empty() && was_not_empty) {
            xEventGroupClearBits(event_group_, AS_EVENT_PLAYBACK_NOT_EMPTY);
            if (callbacks_.on_playback_idle) {
                callbacks_.on_playback_idle();
            }
        }

        lock.unlock();

        // 5. 播放音频
        last_output_time_ = std::chrono::steady_clock::now();
        codec_->WriteAudioData(task->pcm);
    }
}
```

### 4.3 OpusCodecTask

```cpp
void AudioService::OpusCodecTask() {
    while (!service_stopped_) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);

        // 1. 等待编码或解码任务
        audio_queue_cv_.wait(lock, [this]() {
            return !audio_encode_queue_.empty() ||
                   !audio_decode_queue_.empty() ||
                   service_stopped_;
        });

        if (service_stopped_) break;

        // 2. 优先处理解码 (保证播放流畅)
        if (!audio_decode_queue_.empty()) {
            auto packet = std::move(audio_decode_queue_.front());
            audio_decode_queue_.pop_front();
            lock.unlock();

            // Opus 解码
            std::vector<int16_t> pcm;
            opus_decoder_->Decode(packet->payload, pcm);

            // 推送到播放队列
            auto task = std::make_unique<AudioTask>();
            task->type = kAudioTaskTypeDecodeToPlaybackQueue;
            task->pcm = std::move(pcm);

            lock.lock();
            audio_playback_queue_.push_back(std::move(task));
            xEventGroupSetBits(event_group_, AS_EVENT_PLAYBACK_NOT_EMPTY);
            audio_queue_cv_.notify_all();
        }

        // 3. 处理编码
        else if (!audio_encode_queue_.empty()) {
            auto task = std::move(audio_encode_queue_.front());
            audio_encode_queue_.pop_front();
            lock.unlock();

            // Opus 编码
            std::vector<uint8_t> opus_data;
            opus_encoder_->Encode(task->pcm, opus_data);

            // 推送到发送队列
            auto packet = std::make_unique<AudioStreamPacket>();
            packet->sample_rate = 16000;
            packet->frame_duration = OPUS_FRAME_DURATION_MS;
            packet->timestamp = task->timestamp;
            packet->payload = std::move(opus_data);

            lock.lock();
            audio_send_queue_.push_back(std::move(packet));
            // 通知 Application 发送
            if (callbacks_.on_send_queue_available) {
                callbacks_.on_send_queue_available();
            }
        }
    }
}
```

## 5. 队列配置

### 5.1 常量定义

```cpp
#define OPUS_FRAME_DURATION_MS 60          // 60ms 帧时长
#define MAX_ENCODE_TASKS_IN_QUEUE 2        // 编码队列最大任务数
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2      // 播放队列最大任务数
#define MAX_DECODE_PACKETS_IN_QUEUE (2400 / 60)  // 40 帧 = 2.4秒
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / 60)    // 40 帧 = 2.4秒
```

### 5.2 队列容量分析

| 队列 | 容量 | 时长 | 用途 |
|------|------|------|------|
| audio_decode_queue_ | 40 包 | 2.4s | 接收缓冲 |
| audio_playback_queue_ | 40 帧 | 2.4s | 播放缓冲 |
| audio_send_queue_ | 40 包 | 2.4s | 发送缓冲 |
| audio_encode_queue_ | 2 任务 | 120ms | 编码任务 |

## 6. 预缓冲机制

### 6.1 目的
避免网络抖动导致的音频断续

### 6.2 实现

```cpp
// 常量
static const int PREBUFFER_FRAMES = 5;  // 5 * 60ms = 300ms

// 开始预缓冲
void AudioService::StartPrebuffering() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    prebuffering_ = true;
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
}

// AudioOutputTask 中的预缓冲检查
if (prebuffering_) {
    if (audio_playback_queue_.size() < PREBUFFER_FRAMES) {
        continue;  // 继续等待
    }
    prebuffering_ = false;  // 缓冲完成，开始播放
}
```

### 6.3 触发时机

| 事件 | 动作 |
|------|------|
| AUDIO_START | StartPrebuffering() |
| AUDIO_END | StopPrebuffering() |
| Disconnect | ResetDecoder() |

## 7. 硬件抽象

### 7.1 AudioCodec 接口

```cpp
class AudioCodec {
public:
    virtual void Initialize() = 0;
    virtual void EnableInput(bool enable) = 0;
    virtual void EnableOutput(bool enable) = 0;
    virtual bool ReadAudioData(std::vector<int16_t>& data) = 0;
    virtual bool WriteAudioData(const std::vector<int16_t>& data) = 0;

    // 状态查询
    bool input_enabled() const;
    bool output_enabled() const;
};
```

### 7.2 实现类

| 类 | 芯片 | 用途 |
|------|------|------|
| Es8311AudioCodec | ES8311 | 单声道 Codec |
| Es8388AudioCodec | ES8388 | 立体声 Codec |
| Es8374AudioCodec | ES8374 | |
| DummyAudioCodec | - | 测试用 |

## 8. 电源管理

```cpp
#define AUDIO_POWER_TIMEOUT_MS 15000   // 15秒无活动关闭
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000  // 1秒检查一次

void AudioService::CheckAndUpdateAudioPowerState() {
    auto now = std::chrono::steady_clock::now();

    // 检查输入
    auto input_elapsed = duration_cast<milliseconds>(now - last_input_time_).count();
    if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->input_enabled()) {
        codec_->EnableInput(false);
    }

    // 检查输出
    auto output_elapsed = duration_cast<milliseconds>(now - last_output_time_).count();
    if (output_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->output_enabled()) {
        codec_->EnableOutput(false);
    }
}
```

## 9. 回调接口

```cpp
struct AudioServiceCallbacks {
    // 发送队列可用 (有数据待发送)
    std::function<void(void)> on_send_queue_available;

    // 唤醒词检测到
    std::function<void(const std::string&)> on_wake_word_detected;

    // VAD 状态变化
    std::function<void(bool)> on_vad_change;

    // 音频测试队列满
    std::function<void(void)> on_audio_testing_queue_full;

    // 播放队列空 (播放完成)
    std::function<void(void)> on_playback_idle;
};
```

## 10. 最佳实践

### 10.1 缓冲区管理

根据 [ESP-ADF 文档](https://docs.espressif.com/projects/esp-adf/en/latest/) 和 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 官方实现：

| 策略 | 推荐值 | 说明 |
|------|--------|------|
| Ring Buffer | 1-2 秒 | WiFi/4G 延迟抖动可达 300ms+ |
| DMA Buffer | 增大 size | 减少中断频率，降低 CPU 负载 |
| PSRAM | 1MB+ | 内部 RAM 不够大缓冲时使用 |

**关键原则**: 网络数据到达速度不稳定，需要足够缓冲来平滑播放。

### 10.2 任务调度

| 策略 | 说明 |
|------|------|
| 音频输出任务放 Core 0 | 与 opus_codec 同核，减少跨核通信 |
| 高优先级 (9) | 音频输出任务设最高优先级 |
| 音频输入放 Core 1 | 与 AFE 处理同核 |

参考：[arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)

### 10.3 4G 网络特殊处理

4G 网络比 WiFi 抖动更大，需要：

```cpp
// 4G 网络推荐配置
#define MAX_DECODE_PACKETS_IN_QUEUE 200   // 12秒缓冲 (vs WiFi 2.4秒)
#define BUFFER_START_THRESHOLD_FRAMES 10  // 600ms 预缓冲
#define BUFFER_RESUME_THRESHOLD_FRAMES 5  // 300ms 恢复缓冲
```

### 10.4 队列满处理策略

**错误做法** (会丢失数据):
```cpp
if (queue.size() >= MAX) {
    return false;  // 静默丢弃！
}
```

**正确做法** (超时等待):
```cpp
if (queue.size() >= MAX) {
    // 4G 最佳实践：使用超时等待，避免无限阻塞导致 URC 队列溢出
    auto timeout = std::chrono::milliseconds(100);
    if (!cv.wait_for(lock, timeout, [&]() { return queue.size() < MAX; })) {
        // 超时仍满，降级为丢包（保护 URC 处理不被阻塞）
        ESP_LOGW(TAG, "Queue full after timeout, dropping packet!");
        return false;
    }
}
```

> **重要**: 不要使用无限等待 (`cv.wait()`)！4G 模块的 URC 回调和音频接收在同一线程，
> 无限阻塞会导致 URC 队列溢出，丢失 AUDIO_END 等关键消息。

## 11. 已知问题与修复

### 11.1 队列满静默丢包 (已修复)

**问题**: `PushPacketToDecodeQueue` 队列满时返回 false，调用方未检查

**现象**: 服务器发送 1636 帧，设备只收到 60 帧 (96% 丢失)

**修复**:
1. 增大队列容量适应 4G (200 包 = 12 秒)
2. 使用超时等待 (100ms) 而非无限阻塞
3. 添加丢包警告日志

### 11.2 URC 队列溢出 (已修复)

**问题**: 阻塞等待解码队列时，URC 处理被阻塞

**现象**: `AtUart: [URC] Queue full, dropping: MIPURC`

**根因**: 4G 模块的 WebSocket 数据和 URC 回调共用线程，无限阻塞导致 URC 堆积

**修复**: 使用 `wait_for(100ms)` 替代 `wait()`，超时后丢包而非阻塞

### 11.3 播放断续

**问题**: 预缓冲不足或网络抖动

**修复**:
- 增大 BUFFER_START_THRESHOLD_FRAMES (3 → 10)
- 增大 BUFFER_RESUME_THRESHOLD_FRAMES (2 → 5)

### 11.4 任务优先级

**问题**: audio_output 优先级过低 (3)

**修复**: 提升到 9，确保播放流畅

## 12. 配置参考

### 12.1 WiFi 场景 (官方 xiaozhi-esp32)

```cpp
#define MAX_DECODE_PACKETS_IN_QUEUE 40    // 2.4秒
#define MAX_PLAYBACK_TASKS_IN_QUEUE 2     // 实时播放
#define BUFFER_START_THRESHOLD_FRAMES 3   // 180ms
```

### 12.2 4G 场景 (zhengchen-eye)

```cpp
#define MAX_DECODE_PACKETS_IN_QUEUE 200   // 12秒
#define MAX_PLAYBACK_TASKS_IN_QUEUE 10    // 更大缓冲
#define BUFFER_START_THRESHOLD_FRAMES 10  // 600ms
```

### 12.3 内存估算

| 队列 | 容量 | 单包大小 | 总计 |
|------|------|----------|------|
| decode_queue | 200 包 | ~200B | 40KB |
| playback_queue | 10 帧 | ~2KB | 20KB |
| send_queue | 40 包 | ~200B | 8KB |
| **总计** | - | - | **~70KB** |

## 13. 4G 音频流畅播放经验总结

> **背景**: 在 ESP32-S3 + ML307 4G 模块上实现 TTS 音频流畅播放，经历多次调试优化。

### 13.1 问题演进与解决方案

| 阶段 | 问题现象 | 根因分析 | 解决方案 |
|------|----------|----------|----------|
| 1 | 播放断断续续，96%数据丢失 | `PushPacketToDecodeQueue` 返回false未检查 | 使用阻塞等待 |
| 2 | 仍有丢包 | 队列太小(60包)，4G数据突发到达 | 增大到200包(12秒) |
| 3 | 长时间播放良好，结尾卡顿 | 无限阻塞导致URC队列溢出 | 改用超时等待(100ms) |
| 4 | 最终稳定 | - | 综合方案生效 |

### 13.2 核心原理

```
┌─────────────────────────────────────────────────────────────┐
│                    4G 模块 (ML307)                          │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐        │
│  │ URC 回调   │    │ WebSocket  │    │ AT 命令    │        │
│  │ (+MIPURC)  │◄───│ 数据回调   │    │ 发送队列   │        │
│  └─────┬──────┘    └─────┬──────┘    └────────────┘        │
│        │                 │                                  │
│        │  共用同一线程！  │                                  │
│        ▼                 ▼                                  │
│  ┌─────────────────────────────────────────┐               │
│  │         ML307 Receive Task              │               │
│  └─────────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼ (如果这里阻塞)
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 AudioService                       │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐        │
│  │DecodeQueue │───▶│OpusDecoder │───▶│PlaybackQueue│        │
│  │  (200包)   │    └────────────┘    │  (10帧)    │        │
│  └────────────┘                      └────────────┘        │
│       ▲                                                     │
│       │ PushPacketToDecodeQueue()                          │
│       │ 如果无限阻塞 → URC 处理停滞 → AUDIO_END 丢失        │
└─────────────────────────────────────────────────────────────┘
```

**关键洞察**: 4G 模块的 WebSocket 数据回调和 URC 处理共用同一线程，无限阻塞会导致 URC 队列溢出。

### 13.3 最终配置

```cpp
// audio_service.h - 4G 网络优化配置
#define OPUS_FRAME_DURATION_MS 60
#define MAX_DECODE_PACKETS_IN_QUEUE 200   // 12秒缓冲 (关键！)
#define MAX_PLAYBACK_TASKS_IN_QUEUE 10    // 播放缓冲
#define BUFFER_START_THRESHOLD_FRAMES 10  // 600ms 预缓冲
#define BUFFER_RESUME_THRESHOLD_FRAMES 5  // 300ms 恢复缓冲

// audio_service.cc - 超时等待实现
bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait) {
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    if (audio_decode_queue_.size() >= MAX_DECODE_PACKETS_IN_QUEUE) {
        if (wait) {
            // 关键：使用超时等待，避免无限阻塞
            auto timeout = std::chrono::milliseconds(100);
            if (!audio_queue_cv_.wait_for(lock, timeout, [this]() {
                return audio_decode_queue_.size() < MAX_DECODE_PACKETS_IN_QUEUE;
            })) {
                // 超时后丢包，但不阻塞 URC 处理
                ESP_LOGW(TAG, "Decode queue full after timeout, dropping packet");
                return false;
            }
        } else {
            return false;
        }
    }
    audio_decode_queue_.push_back(std::move(packet));
    audio_queue_cv_.notify_all();
    return true;
}
```

### 13.4 关键经验

| 类别 | 错误做法 | 正确做法 |
|------|----------|----------|
| 队列满处理 | `return false` 静默丢弃 | 超时等待 + 日志 |
| 阻塞方式 | `cv.wait()` 无限等待 | `cv.wait_for(100ms)` |
| 队列容量 | WiFi 配置 (40包) | 4G 专用 (200包) |
| 预缓冲 | 180ms | 600ms (4G抖动大) |
| 日志 | 仅错误 | 关键状态都记录 |

### 13.5 调试技巧

**日志关键词**:
```bash
# 丢包检测
grep -E "dropping|Queue full|underrun" /dev/ttyACM0

# 队列状态
grep "Queue:" /dev/ttyACM0  # 输出: Queue: D=200, P=10, Heap=xxx

# URC 溢出
grep "URC.*Queue full" /dev/ttyACM0
```

**正常播放日志特征**:
```
I WS: ==> AUDIO_START
I AudioService: Queue: D=200, P=10, Heap=...  // D 保持稳定
I AudioService: Queue: D=199, P=10, Heap=...  // 缓慢下降
...
I WS: ==> AUDIO_END
I AudioService: Playback complete
```

**异常日志特征**:
```
W AtUart: [URC] Queue full, dropping: MIPURC  // URC 溢出 - 需要减少阻塞
W AudioService: Buffer underrun  // 缓冲耗尽 - 需要增大队列或预缓冲
W AudioService: Decode queue full after timeout  // 超时丢包 - 正常降级
```

### 13.6 4G vs WiFi 差异

| 特性 | WiFi | 4G (ML307) |
|------|------|------------|
| 延迟 | 10-50ms | 50-300ms |
| 抖动 | 低 | 高 (需更大缓冲) |
| 数据到达 | 均匀 | 突发 (整秒数据一次到达) |
| 回调线程 | 独立 | 与 URC 共用 |
| 推荐队列 | 40包 (2.4秒) | 200包 (12秒) |
| 阻塞策略 | 可无限等待 | 必须超时 |

### 13.7 代码变更清单

| 文件 | 变更 | 目的 |
|------|------|------|
| `audio_service.h` | `MAX_DECODE_PACKETS_IN_QUEUE`: 60→200 | 增大缓冲 |
| `audio_service.h` | `BUFFER_START_THRESHOLD_FRAMES`: 3→10 | 增大预缓冲 |
| `audio_service.cc` | `wait()` → `wait_for(100ms)` | 避免无限阻塞 |
| `application.cc` | `PushPacketToDecodeQueue(packet, true)` | 启用等待模式 |

## 14. 参考资料

- [ESP-ADF GitHub](https://github.com/espressif/esp-adf)
- [xiaozhi-esp32 GitHub](https://github.com/78/xiaozhi-esp32)
- [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)
- [ESP-ADF Ring Buffer](https://docs.espressif.com/projects/esp-adf/en/latest/api-reference/abstraction/ringbuf.html)
- [ESP-IDF I2S](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
- [Phil Schatzmann - Opus Codec](https://www.pschatzmann.ch/home/2022/05/06/audio-streaming-the-opus-codec/)

---

*文档版本: 3.0 - 2024-12-24 添加 4G 音频流畅播放完整经验总结*
