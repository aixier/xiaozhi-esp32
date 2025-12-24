#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include "emotion_state.h"
#include "core/event_bus.h"

#include <esp_timer.h>

#include <string>
#include <functional>

class Display;

/**
 * 显示引擎
 *
 * 功能:
 * - 订阅事件并更新显示
 * - 管理表情状态
 * - 处理文本显示
 * - 省电模式控制
 *
 * 事件到显示映射:
 * - CONN_STARTING       -> thinking + "连接中..."
 * - CONN_SUCCESS        -> neutral + "已连接"
 * - CONN_FAILED         -> sad + 错误信息
 * - CONN_DISCONNECTED   -> confused + "已断开"
 * - AUDIO_OUTPUT_START  -> 服务器指定表情
 * - AUDIO_PLAYBACK_COMPLETE -> neutral (延迟)
 * - DISPLAY_SET_EMOTION -> 更新表情
 * - DISPLAY_SET_TEXT    -> 更新文本
 * - SYSTEM_ERROR        -> sad/angry + 错误信息
 * - SYSTEM_IDLE_TIMEOUT -> sleepy + 进入省电
 */
class DisplayEngine {
public:
    /**
     * 省电模式状态
     */
    enum PowerMode {
        POWER_NORMAL,   // 正常模式
        POWER_DIM,      // 变暗模式
        POWER_SLEEP,    // 睡眠模式
    };

    /**
     * 回调函数
     */
    struct Callbacks {
        std::function<void(const std::string& emotion)> set_emotion;
        std::function<void(const std::string& text)> set_chat_message;
        std::function<void(const std::string& status)> set_status;
        std::function<void(int brightness)> set_brightness;
    };

    DisplayEngine();
    ~DisplayEngine();

    /**
     * 初始化
     * @param display 显示实例
     */
    void Initialize(Display* display);

    /**
     * 设置回调
     */
    void SetCallbacks(const Callbacks& callbacks);

    /**
     * 获取表情状态管理器
     */
    EmotionState& GetEmotionState();

    /**
     * 通知用户活动 (重置省电计时器)
     */
    void OnUserActivity();

    /**
     * 获取当前省电模式
     */
    PowerMode GetPowerMode() const;

    /**
     * 手动设置省电模式
     */
    void SetPowerMode(PowerMode mode);

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
    void OnConnectionStarting(const Event& e);
    void OnConnectionSuccess(const Event& e);
    void OnConnectionFailed(const Event& e);
    void OnConnectionDisconnected(const Event& e);
    void OnConnectionReconnecting(const Event& e);
    void OnAudioPlaybackStarted(const Event& e);
    void OnAudioPlaybackComplete(const Event& e);
    void OnDisplaySetEmotion(const Event& e);
    void OnDisplaySetText(const Event& e);
    void OnDisplaySetStatus(const Event& e);
    void OnSystemError(const Event& e);
    void OnSystemIdleTimeout(const Event& e);
    void OnUserButtonPressed(const Event& e);
    void OnUserWakeWord(const Event& e);

    /**
     * 省电定时器回调
     */
    void OnIdleTimer();

    /**
     * 播放完成后延迟恢复定时器
     */
    void OnRestoreTimer();

    // 省电配置
    static const int DIM_TIMEOUT_MS = 30000;    // 30秒变暗
    static const int SLEEP_TIMEOUT_MS = 60000;  // 60秒睡眠
    static const int RESTORE_DELAY_MS = 2000;   // 播放完成后 2 秒恢复

    Display* display_ = nullptr;
    EmotionState emotion_state_;
    Callbacks callbacks_;
    PowerMode power_mode_ = POWER_NORMAL;

    // 定时器
    esp_timer_handle_t idle_timer_ = nullptr;
    esp_timer_handle_t restore_timer_ = nullptr;

    // 订阅 ID
    int sub_conn_starting_ = -1;
    int sub_conn_success_ = -1;
    int sub_conn_failed_ = -1;
    int sub_conn_disconnected_ = -1;
    int sub_conn_reconnecting_ = -1;
    int sub_audio_started_ = -1;
    int sub_audio_complete_ = -1;
    int sub_display_emotion_ = -1;
    int sub_display_text_ = -1;
    int sub_display_status_ = -1;
    int sub_system_error_ = -1;
    int sub_system_idle_ = -1;
    int sub_user_button_ = -1;
    int sub_user_wake_ = -1;
};

#endif // DISPLAY_ENGINE_H
