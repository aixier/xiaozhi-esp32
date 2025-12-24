#include "audio_player.h"
#include "audio_service.h"

#include <esp_log.h>

static const char* TAG = "AudioPlayer";

AudioPlayer::AudioPlayer() {
}

AudioPlayer::~AudioPlayer() {
    UnsubscribeEvents();
}

void AudioPlayer::Initialize(AudioCodec* codec, AudioService* service) {
    codec_ = codec;
    service_ = service;

    // 设置播放控制器回调
    PlaybackController::Callbacks callbacks;

    callbacks.on_start_playback = [this]() {
        ESP_LOGI(TAG, "Start playback callback");
        // 实际播放由 AudioService 的解码循环处理
    };

    callbacks.on_playback_complete = [this]() {
        ESP_LOGI(TAG, "Playback complete callback");
    };

    callbacks.on_buffer_low = [this]() {
        ESP_LOGW(TAG, "Buffer low callback");
    };

    callbacks.get_queued_frames = [this]() -> int {
        // TODO: 从 AudioService 获取队列帧数
        return 0;
    };

    callbacks.get_buffered_frames = [this]() -> int {
        // TODO: 从 AudioService 获取缓冲帧数
        return 0;
    };

    controller_.SetCallbacks(callbacks);

    // 订阅事件
    SubscribeEvents();

    ESP_LOGI(TAG, "Initialized");
}

void AudioPlayer::Stop() {
    ESP_LOGI(TAG, "Stop requested");
    controller_.Reset();
}

PlaybackController& AudioPlayer::GetController() {
    return controller_;
}

PlaybackController::State AudioPlayer::GetState() const {
    return controller_.GetState();
}

bool AudioPlayer::IsPlaying() const {
    auto state = controller_.GetState();
    return state == PlaybackController::PLAYING ||
           state == PlaybackController::DRAINING;
}

void AudioPlayer::SubscribeEvents() {
    auto& bus = EventBus::GetInstance();

    sub_audio_start_ = bus.Subscribe(
        EventType::AUDIO_OUTPUT_START,
        [this](const Event& e) { OnAudioOutputStart(e); }
    );

    sub_audio_data_ = bus.Subscribe(
        EventType::AUDIO_OUTPUT_DATA,
        [this](const Event& e) { OnAudioOutputData(e); }
    );

    sub_audio_end_ = bus.Subscribe(
        EventType::AUDIO_OUTPUT_END,
        [this](const Event& e) { OnAudioOutputEnd(e); }
    );

    sub_conn_disconnected_ = bus.Subscribe(
        EventType::CONN_DISCONNECTED,
        [this](const Event& e) { OnConnectionLost(e); }
    );

    sub_conn_failed_ = bus.Subscribe(
        EventType::CONN_FAILED,
        [this](const Event& e) { OnConnectionLost(e); }
    );

    ESP_LOGD(TAG, "Subscribed to events");
}

void AudioPlayer::UnsubscribeEvents() {
    auto& bus = EventBus::GetInstance();

    if (sub_audio_start_ >= 0) {
        bus.Unsubscribe(EventType::AUDIO_OUTPUT_START, sub_audio_start_);
    }
    if (sub_audio_data_ >= 0) {
        bus.Unsubscribe(EventType::AUDIO_OUTPUT_DATA, sub_audio_data_);
    }
    if (sub_audio_end_ >= 0) {
        bus.Unsubscribe(EventType::AUDIO_OUTPUT_END, sub_audio_end_);
    }
    if (sub_conn_disconnected_ >= 0) {
        bus.Unsubscribe(EventType::CONN_DISCONNECTED, sub_conn_disconnected_);
    }
    if (sub_conn_failed_ >= 0) {
        bus.Unsubscribe(EventType::CONN_FAILED, sub_conn_failed_);
    }

    ESP_LOGD(TAG, "Unsubscribed from events");
}

void AudioPlayer::OnAudioOutputStart(const Event& e) {
    ESP_LOGI(TAG, "Audio output start");
    controller_.OnAudioStart();
}

void AudioPlayer::OnAudioOutputData(const Event& e) {
    const auto* audio_event = static_cast<const AudioDataEvent*>(&e);

    // 计算音频时长 (基于数据大小和 Opus 帧)
    int duration_ms = audio_event->duration_ms;
    if (duration_ms <= 0) {
        // 默认 Opus 帧 60ms
        duration_ms = 60;
    }

    controller_.OnAudioData(duration_ms);
}

void AudioPlayer::OnAudioOutputEnd(const Event& e) {
    ESP_LOGI(TAG, "Audio output end");
    controller_.OnAudioEnd();
}

void AudioPlayer::OnConnectionLost(const Event& e) {
    ESP_LOGW(TAG, "Connection lost, stopping playback");
    Stop();
}
