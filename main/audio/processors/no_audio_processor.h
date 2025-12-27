/**
 * @file no_audio_processor.h
 * @brief PSM-ESP32-MED-001: MED-P002 空操作音频处理器实现
 * @trace PIM-MED-001 媒体域需求规格 - AudioProcessor 接口实现 (无处理透传)
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef DUMMY_AUDIO_PROCESSOR_H
#define DUMMY_AUDIO_PROCESSOR_H

#include <vector>
#include <functional>

#include "audio_processor.h"
#include "audio_codec.h"

class NoAudioProcessor : public AudioProcessor {
public:
    NoAudioProcessor() = default;
    ~NoAudioProcessor() = default;

    void Initialize(AudioCodec* codec, int frame_duration_ms) override;
    void Feed(std::vector<int16_t>&& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    AudioCodec* codec_ = nullptr;
    int frame_samples_ = 0;
    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;
    bool is_running_ = false;
};

#endif 