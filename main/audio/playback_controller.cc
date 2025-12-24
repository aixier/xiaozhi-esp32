#include "playback_controller.h"
#include "core/event_bus.h"

#include <esp_log.h>

static const char* TAG = "PlaybackCtrl";

PlaybackController::PlaybackController() {
    // 创建完成延迟定时器
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<PlaybackController*>(arg)->OnCompleteTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "playback_complete",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&timer_args, &complete_timer_);
}

PlaybackController::~PlaybackController() {
    if (complete_timer_) {
        esp_timer_stop(complete_timer_);
        esp_timer_delete(complete_timer_);
    }
}

void PlaybackController::SetCallbacks(const Callbacks& callbacks) {
    callbacks_ = callbacks;
}

void PlaybackController::OnAudioStart() {
    if (state_ != IDLE) {
        ESP_LOGW(TAG, "OnAudioStart in state %d, resetting", state_);
        Reset();
    }

    state_ = BUFFERING;
    buffered_ms_ = 0;
    audio_end_received_ = false;
    low_water_warned_ = false;

    ESP_LOGI(TAG, "Audio start, entering BUFFERING state");
}

void PlaybackController::OnAudioData(int duration_ms) {
    if (state_ == IDLE || state_ == COMPLETE) {
        ESP_LOGW(TAG, "OnAudioData in state %d, ignoring", state_);
        return;
    }

    buffered_ms_ += duration_ms;

    if (state_ == BUFFERING) {
        if (buffered_ms_ >= PREBUFFER_MS) {
            ESP_LOGI(TAG, "Prebuffer complete (%d ms), starting playback", buffered_ms_.load());

            state_ = PLAYING;

            // 触发开始播放
            if (callbacks_.on_start_playback) {
                callbacks_.on_start_playback();
            }

            // 发布事件
            Event event(EventType::AUDIO_PLAYBACK_STARTED);
            EventBus::GetInstance().Emit(event);
        }
    }
}

void PlaybackController::OnAudioEnd() {
    if (state_ == IDLE || state_ == COMPLETE) {
        ESP_LOGW(TAG, "OnAudioEnd in state %d, ignoring", state_);
        return;
    }

    audio_end_received_ = true;
    ESP_LOGI(TAG, "Audio end received, buffered: %d ms", buffered_ms_.load());

    if (state_ == BUFFERING) {
        // 还没开始播放就收到 END，直接开始播放
        ESP_LOGW(TAG, "Audio end during buffering, starting playback with %d ms", buffered_ms_.load());

        state_ = DRAINING;

        if (callbacks_.on_start_playback) {
            callbacks_.on_start_playback();
        }

        Event event(EventType::AUDIO_PLAYBACK_STARTED);
        EventBus::GetInstance().Emit(event);
    } else if (state_ == PLAYING) {
        state_ = DRAINING;
        ESP_LOGI(TAG, "Entering DRAINING state");
    }
}

void PlaybackController::OnPlaybackTick() {
    if (state_ == IDLE || state_ == COMPLETE) {
        return;
    }

    // 更新缓冲状态
    int queued = 0;
    int buffered = 0;

    if (callbacks_.get_queued_frames) {
        queued = callbacks_.get_queued_frames();
    }
    if (callbacks_.get_buffered_frames) {
        buffered = callbacks_.get_buffered_frames();
    }

    int total_frames = queued + buffered;
    int estimated_ms = total_frames * FRAME_DURATION_MS;

    // 低水位警告
    if (state_ == PLAYING && !audio_end_received_ && estimated_ms < LOW_WATER_MS) {
        if (!low_water_warned_) {
            low_water_warned_ = true;
            ESP_LOGW(TAG, "Buffer low: %d ms", estimated_ms);

            Event event(EventType::AUDIO_BUFFER_LOW);
            EventBus::GetInstance().Emit(event);

            if (callbacks_.on_buffer_low) {
                callbacks_.on_buffer_low();
            }
        }
    } else {
        low_water_warned_ = false;
    }

    // 检查播放完成
    if (state_ == DRAINING) {
        CheckPlaybackComplete();
    }
}

void PlaybackController::Reset() {
    if (complete_timer_) {
        esp_timer_stop(complete_timer_);
    }

    state_ = IDLE;
    buffered_ms_ = 0;
    audio_end_received_ = false;
    low_water_warned_ = false;

    ESP_LOGD(TAG, "Reset to IDLE");
}

PlaybackController::State PlaybackController::GetState() const {
    return state_;
}

int PlaybackController::GetBufferedMs() const {
    return buffered_ms_;
}

bool PlaybackController::CanStartPlayback() const {
    return state_ == PLAYING || state_ == DRAINING;
}

void PlaybackController::CheckPlaybackComplete() {
    if (state_ != DRAINING) {
        return;
    }

    if (!audio_end_received_) {
        return;
    }

    // 检查队列是否已空
    int queued = 0;
    int buffered = 0;

    if (callbacks_.get_queued_frames) {
        queued = callbacks_.get_queued_frames();
    }
    if (callbacks_.get_buffered_frames) {
        buffered = callbacks_.get_buffered_frames();
    }

    if (queued == 0 && buffered == 0) {
        ESP_LOGI(TAG, "All audio played, scheduling completion");

        state_ = COMPLETE;

        // 延迟触发完成事件
        if (complete_timer_) {
            esp_timer_start_once(complete_timer_, COMPLETE_DELAY_MS * 1000);
        }
    }
}

void PlaybackController::OnCompleteTimer() {
    ESP_LOGI(TAG, "Playback complete");

    // 发布播放完成事件
    Event event(EventType::AUDIO_PLAYBACK_COMPLETE);
    EventBus::GetInstance().Emit(event);

    if (callbacks_.on_playback_complete) {
        callbacks_.on_playback_complete();
    }

    // 重置状态
    state_ = IDLE;
}
