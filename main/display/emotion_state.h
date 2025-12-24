#ifndef EMOTION_STATE_H
#define EMOTION_STATE_H

#include <esp_timer.h>

#include <string>
#include <map>
#include <functional>

/**
 * 表情状态管理器
 *
 * 功能:
 * - 管理当前表情状态
 * - 实现情感过渡逻辑 (避免突兀切换)
 * - 支持表情持续时间控制
 *
 * 情感类别:
 * - POSITIVE: happy, love, winking, cool
 * - NEUTRAL:  neutral, thinking, confused, sleepy
 * - NEGATIVE: sad, crying, angry
 *
 * 过渡规则:
 * - 跨类别切换需要经过 neutral
 * - 例如: happy -> sad 会变成 happy -> neutral -> sad
 */
class EmotionState {
public:
    /**
     * 情感类别
     */
    enum Category {
        POSITIVE,   // 积极
        NEUTRAL,    // 中性
        NEGATIVE,   // 消极
    };

    /**
     * 回调函数
     */
    struct Callbacks {
        std::function<void(const std::string& emotion)> on_emotion_change;
        std::function<void(const std::string& from, const std::string& to)> on_transition;
    };

    EmotionState();
    ~EmotionState();

    /**
     * 设置回调
     */
    void SetCallbacks(const Callbacks& callbacks);

    /**
     * 设置表情 (立即切换)
     */
    void SetEmotion(const std::string& emotion);

    /**
     * 过渡到目标表情 (可能需要中间状态)
     */
    void TransitionTo(const std::string& target);

    /**
     * 设置临时表情 (一段时间后恢复)
     * @param emotion 临时表情
     * @param duration_ms 持续时间
     * @param restore_to 恢复到的表情 (空字符串=恢复到之前的)
     */
    void SetTemporary(const std::string& emotion, int duration_ms,
                      const std::string& restore_to = "");

    /**
     * 获取当前表情
     */
    std::string GetCurrent() const;

    /**
     * 获取表情的情感类别
     */
    Category GetCategory(const std::string& emotion) const;

    /**
     * 检查是否需要过渡
     */
    bool NeedsTransition(const std::string& from, const std::string& to) const;

    /**
     * 获取过渡中间状态
     */
    std::string GetTransitionMiddle(const std::string& from, const std::string& to) const;

    /**
     * 重置到默认状态
     */
    void Reset();

private:
    /**
     * 过渡定时器回调
     */
    void OnTransitionTimer();

    /**
     * 临时表情恢复定时器回调
     */
    void OnRestoreTimer();

    std::string current_ = "neutral";
    std::string previous_ = "neutral";
    std::string transition_target_;
    std::string restore_to_;

    Callbacks callbacks_;

    // 表情->类别映射
    std::map<std::string, Category> category_map_;

    esp_timer_handle_t transition_timer_ = nullptr;
    esp_timer_handle_t restore_timer_ = nullptr;

    // 过渡延迟
    static const int TRANSITION_DELAY_MS = 300;
};

#endif // EMOTION_STATE_H
