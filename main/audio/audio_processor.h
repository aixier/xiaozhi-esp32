/**
 * @file audio_processor.h
 * @brief PSM-ESP32-MED-001: MED-I001 AudioProcessor 音频处理器接口
 * @trace PIM-MED-001 媒体域需求规格
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <string>
#include <vector>
#include <functional>

#include "audio_codec.h"

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    
    virtual void Initialize(AudioCodec* codec, int frame_duration_ms) = 0;
    virtual void Feed(std::vector<int16_t>&& data) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() = 0;
    virtual void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) = 0;
    virtual void OnVadStateChange(std::function<void(bool speaking)> callback) = 0;
    virtual size_t GetFeedSize() = 0;
    virtual void EnableDeviceAec(bool enable) = 0;
};

#endif
