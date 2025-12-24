#ifndef PLAYBACK_CONTROLLER_H
#define PLAYBACK_CONTROLLER_H

#include <freertos/FreeRTOS.h>
#include <esp_timer.h>

#include <vector>
#include <cstdint>
#include <atomic>
#include <functional>

/**
 * 播放控制器
 *
 * 功能:
 * - 预缓冲: 收到足够音频数据后再开始播放，避免卡顿
 * - 播放完成检测: 确保所有音频数据播放完毕后才触发完成事件
 * - 缓冲状态监控: 低水位警告
 *
 * 状态机:
 * ```
 *     IDLE
 *       │
 *   OnAudioStart()
 *       ▼
 *   BUFFERING ──── buffered >= PREBUFFER_MS ────▶ PLAYING
 *       │                                            │
 *   OnAudioEnd()                               OnAudioEnd()
 *       ▼                                            ▼
 *   DRAINING ◀─────────────────────────────── DRAINING
 *       │
 *   queue empty + delay
 *       ▼
 *   COMPLETE ──▶ IDLE
 * ```
 */
class PlaybackController {
public:
    /**
     * 播放状态
     */
    enum State {
        IDLE,       // 空闲
        BUFFERING,  // 预缓冲中 (收集数据)
        PLAYING,    // 播放中
        DRAINING,   // 排空队列 (收到 AUDIO_END，等待播放完成)
        COMPLETE,   // 播放完成
    };

    /**
     * 回调函数
     */
    struct Callbacks {
        std::function<void()> on_start_playback;    // 开始实际播放
        std::function<void()> on_playback_complete; // 播放完成
        std::function<void()> on_buffer_low;        // 缓冲区低水位
        std::function<int()> get_buffered_frames;   // 获取缓冲帧数
        std::function<int()> get_queued_frames;     // 获取队列帧数
    };

    PlaybackController();
    ~PlaybackController();

    /**
     * 设置回调
     */
    void SetCallbacks(const Callbacks& callbacks);

    /**
     * 收到 AUDIO_START
     */
    void OnAudioStart();

    /**
     * 收到音频数据
     * @param duration_ms 本帧音频时长
     */
    void OnAudioData(int duration_ms);

    /**
     * 收到 AUDIO_END
     */
    void OnAudioEnd();

    /**
     * 播放任务回调 (每帧调用)
     * 检查缓冲状态、播放完成等
     */
    void OnPlaybackTick();

    /**
     * 重置状态
     */
    void Reset();

    /**
     * 获取当前状态
     */
    State GetState() const;

    /**
     * 获取已缓冲时长 (ms)
     */
    int GetBufferedMs() const;

    /**
     * 检查是否可以开始播放
     */
    bool CanStartPlayback() const;

private:
    /**
     * 检查播放完成
     */
    void CheckPlaybackComplete();

    /**
     * 完成延迟定时器回调
     */
    void OnCompleteTimer();

    // 预缓冲配置 (4G 网络抖动可达 2 秒，需要足够缓冲)
    static const int PREBUFFER_MS = 1800;      // 预缓冲时长 1800ms
    static const int LOW_WATER_MS = 100;       // 低水位警告 100ms
    static const int COMPLETE_DELAY_MS = 200;  // 播放完成后延迟 200ms

    // 每帧 Opus 音频时长 (默认 60ms)
    static const int FRAME_DURATION_MS = 60;

    State state_ = IDLE;
    Callbacks callbacks_;
    std::atomic<int> buffered_ms_{0};
    std::atomic<bool> audio_end_received_{false};
    bool low_water_warned_ = false;

    esp_timer_handle_t complete_timer_ = nullptr;
};

#endif // PLAYBACK_CONTROLLER_H
