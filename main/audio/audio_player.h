/**
 * @file audio_player.h
 * @brief PSM-ESP32-MED-001: MED-C003 AudioPlayer 播放器
 * @trace PIM-MED-001 媒体域需求规格
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "playback_controller.h"
#include "core/event_bus.h"

#include <vector>
#include <cstdint>

class AudioCodec;
class AudioService;

/**
 * 音频播放器
 *
 * 功能:
 * - 订阅音频相关事件
 * - 管理播放控制器
 * - 协调 AudioService
 *
 * 使用示例:
 * ```cpp
 * AudioPlayer player;
 * player.Initialize(codec, service);
 *
 * // 事件自动处理:
 * // AUDIO_OUTPUT_START -> 预缓冲开始
 * // AUDIO_OUTPUT_DATA  -> 数据入队
 * // AUDIO_OUTPUT_END   -> 排空队列
 * // AUDIO_PLAYBACK_COMPLETE -> 播放完成
 * ```
 */
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    /**
     * 初始化
     * @param codec 音频编解码器
     * @param service 音频服务 (可选，用于直接控制)
     */
    void Initialize(AudioCodec* codec, AudioService* service = nullptr);

    /**
     * 停止播放
     */
    void Stop();

    /**
     * 获取播放控制器
     */
    PlaybackController& GetController();

    /**
     * 获取播放状态
     */
    PlaybackController::State GetState() const;

    /**
     * 检查是否正在播放
     */
    bool IsPlaying() const;

private:
    /**
     * 订阅事件
     */
    void SubscribeEvents();

    /**
     * 取消订阅
     */
    void UnsubscribeEvents();

    // 事件处理器
    void OnAudioOutputStart(const Event& e);
    void OnAudioOutputData(const Event& e);
    void OnAudioOutputEnd(const Event& e);
    void OnConnectionLost(const Event& e);

    PlaybackController controller_;
    AudioCodec* codec_ = nullptr;
    AudioService* service_ = nullptr;

    // 订阅 ID
    int sub_audio_start_ = -1;
    int sub_audio_data_ = -1;
    int sub_audio_end_ = -1;
    int sub_conn_disconnected_ = -1;
    int sub_conn_failed_ = -1;
};

#endif // AUDIO_PLAYER_H
