/**
 * @file wake_word.h
 * @brief PSM-ESP32-MED-001: MED-I002 WakeWord 唤醒词检测接口
 * @trace PIM-MED-001 媒体域需求规格
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef WAKE_WORD_H
#define WAKE_WORD_H

#include <string>
#include <vector>
#include <functional>

#include "audio_codec.h"

class WakeWord {
public:
    virtual ~WakeWord() = default;
    
    virtual bool Initialize(AudioCodec* codec) = 0;
    virtual void Feed(const std::vector<int16_t>& data) = 0;
    virtual void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual size_t GetFeedSize() = 0;
    virtual void EncodeWakeWordData() = 0;
    virtual bool GetWakeWordOpus(std::vector<uint8_t>& opus) = 0;
    virtual const std::string& GetLastDetectedWakeWord() const = 0;
};

#endif
