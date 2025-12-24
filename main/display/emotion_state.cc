#include "emotion_state.h"

#include <esp_log.h>

static const char* TAG = "EmotionState";

EmotionState::EmotionState() {
    // 初始化表情类别映射
    category_map_["happy"] = POSITIVE;
    category_map_["love"] = POSITIVE;
    category_map_["winking"] = POSITIVE;
    category_map_["cool"] = POSITIVE;
    category_map_["excited"] = POSITIVE;
    category_map_["laughing"] = POSITIVE;

    category_map_["neutral"] = NEUTRAL;
    category_map_["thinking"] = NEUTRAL;
    category_map_["confused"] = NEUTRAL;
    category_map_["sleepy"] = NEUTRAL;
    category_map_["surprised"] = NEUTRAL;
    category_map_["curious"] = NEUTRAL;

    category_map_["sad"] = NEGATIVE;
    category_map_["crying"] = NEGATIVE;
    category_map_["angry"] = NEGATIVE;
    category_map_["scared"] = NEGATIVE;
    category_map_["embarrassed"] = NEGATIVE;
    category_map_["worried"] = NEGATIVE;

    // 创建过渡定时器
    esp_timer_create_args_t transition_args = {
        .callback = [](void* arg) {
            static_cast<EmotionState*>(arg)->OnTransitionTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emotion_transition",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&transition_args, &transition_timer_);

    // 创建恢复定时器
    esp_timer_create_args_t restore_args = {
        .callback = [](void* arg) {
            static_cast<EmotionState*>(arg)->OnRestoreTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emotion_restore",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&restore_args, &restore_timer_);
}

EmotionState::~EmotionState() {
    if (transition_timer_) {
        esp_timer_stop(transition_timer_);
        esp_timer_delete(transition_timer_);
    }
    if (restore_timer_) {
        esp_timer_stop(restore_timer_);
        esp_timer_delete(restore_timer_);
    }
}

void EmotionState::SetCallbacks(const Callbacks& callbacks) {
    callbacks_ = callbacks;
}

void EmotionState::SetEmotion(const std::string& emotion) {
    if (current_ == emotion) {
        ESP_LOGI(TAG, "Emotion unchanged: %s", emotion.c_str());
        return;
    }

    // 停止进行中的过渡
    if (transition_timer_) {
        esp_timer_stop(transition_timer_);
    }

    previous_ = current_;
    current_ = emotion;

    ESP_LOGI(TAG, "Emotion: %s -> %s", previous_.c_str(), current_.c_str());

    if (callbacks_.on_emotion_change) {
        ESP_LOGI(TAG, "Calling on_emotion_change callback");
        callbacks_.on_emotion_change(current_);
    } else {
        ESP_LOGW(TAG, "No on_emotion_change callback set!");
    }
}

void EmotionState::TransitionTo(const std::string& target) {
    if (current_ == target) {
        return;
    }

    if (!NeedsTransition(current_, target)) {
        // 不需要过渡，直接切换
        SetEmotion(target);
        return;
    }

    // 需要过渡: 先切到中间状态，再切到目标
    std::string middle = GetTransitionMiddle(current_, target);

    ESP_LOGI(TAG, "Transition: %s -> %s -> %s",
             current_.c_str(), middle.c_str(), target.c_str());

    // 保存目标
    transition_target_ = target;

    // 先切到中间状态
    SetEmotion(middle);

    if (callbacks_.on_transition) {
        callbacks_.on_transition(current_, target);
    }

    // 延迟后切到目标
    if (transition_timer_) {
        esp_timer_start_once(transition_timer_, TRANSITION_DELAY_MS * 1000);
    }
}

void EmotionState::SetTemporary(const std::string& emotion, int duration_ms,
                                const std::string& restore_to) {
    // 保存恢复目标
    if (restore_to.empty()) {
        restore_to_ = current_;
    } else {
        restore_to_ = restore_to;
    }

    // 设置临时表情
    SetEmotion(emotion);

    // 启动恢复定时器
    if (restore_timer_) {
        esp_timer_stop(restore_timer_);
        esp_timer_start_once(restore_timer_, duration_ms * 1000);
    }

    ESP_LOGD(TAG, "Temporary emotion: %s for %d ms, restore to: %s",
             emotion.c_str(), duration_ms, restore_to_.c_str());
}

std::string EmotionState::GetCurrent() const {
    return current_;
}

EmotionState::Category EmotionState::GetCategory(const std::string& emotion) const {
    auto it = category_map_.find(emotion);
    if (it != category_map_.end()) {
        return it->second;
    }
    // 未知表情默认为中性
    return NEUTRAL;
}

bool EmotionState::NeedsTransition(const std::string& from, const std::string& to) const {
    Category from_cat = GetCategory(from);
    Category to_cat = GetCategory(to);

    // 只有跨越 POSITIVE 和 NEGATIVE 时需要过渡
    // POSITIVE -> NEGATIVE 或 NEGATIVE -> POSITIVE
    if ((from_cat == POSITIVE && to_cat == NEGATIVE) ||
        (from_cat == NEGATIVE && to_cat == POSITIVE)) {
        return true;
    }

    return false;
}

std::string EmotionState::GetTransitionMiddle(const std::string& from,
                                               const std::string& to) const {
    // 默认用 neutral 作为过渡
    return "neutral";
}

void EmotionState::Reset() {
    if (transition_timer_) {
        esp_timer_stop(transition_timer_);
    }
    if (restore_timer_) {
        esp_timer_stop(restore_timer_);
    }

    current_ = "neutral";
    previous_ = "neutral";
    transition_target_.clear();
    restore_to_.clear();

    ESP_LOGD(TAG, "Reset to neutral");
}

void EmotionState::OnTransitionTimer() {
    if (!transition_target_.empty()) {
        SetEmotion(transition_target_);
        transition_target_.clear();
    }
}

void EmotionState::OnRestoreTimer() {
    if (!restore_to_.empty()) {
        ESP_LOGD(TAG, "Restoring emotion to: %s", restore_to_.c_str());
        SetEmotion(restore_to_);
        restore_to_.clear();
    }
}
