#include "event_bridge.h"

#include <esp_timer.h>
#include <cstring>

// ========== 连接事件 ==========

void EventBridge::EmitConnectionStart() {
    ConnectionEvent event(EventType::CONN_STARTING);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitConnectionSuccess() {
    ConnectionEvent event(EventType::CONN_SUCCESS);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitConnectionFailed(int error_code, const char* message) {
    ConnectionEvent event(EventType::CONN_FAILED);
    event.timestamp = esp_timer_get_time() / 1000;
    event.error_code = error_code;
    if (message) {
        event.error_message = message;
    }
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitConnectionDisconnected() {
    ConnectionEvent event(EventType::CONN_DISCONNECTED);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitConnectionReconnecting(int retry_count) {
    ConnectionEvent event(EventType::CONN_RECONNECTING);
    event.timestamp = esp_timer_get_time() / 1000;
    event.retry_count = retry_count;
    EventBus::GetInstance().Emit(event);
}

// ========== 音频事件 ==========

void EventBridge::EmitAudioOutputStart() {
    Event event(EventType::AUDIO_OUTPUT_START);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitAudioOutputData(const uint8_t* data, size_t len, int duration_ms) {
    AudioDataEvent event(EventType::AUDIO_OUTPUT_DATA);
    event.timestamp = esp_timer_get_time() / 1000;
    event.duration_ms = duration_ms;
    if (data && len > 0) {
        event.data.assign(data, data + len);
    }
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitAudioOutputEnd() {
    Event event(EventType::AUDIO_OUTPUT_END);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitAudioInputStart() {
    Event event(EventType::AUDIO_INPUT_START);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitAudioInputEnd() {
    Event event(EventType::AUDIO_INPUT_END);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

// ========== 显示事件 ==========

void EventBridge::EmitSetEmotion(const char* emotion) {
    DisplayEvent event(EventType::DISPLAY_SET_EMOTION);
    event.timestamp = esp_timer_get_time() / 1000;
    if (emotion) {
        event.emotion = emotion;
    }
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitSetText(const char* text, const char* role) {
    DisplayEvent event(EventType::DISPLAY_SET_TEXT);
    event.timestamp = esp_timer_get_time() / 1000;
    if (text) {
        event.text = text;
    }
    if (role) {
        event.role = role;
    }
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitSetStatus(const char* status) {
    DisplayEvent event(EventType::DISPLAY_SET_STATUS);
    event.timestamp = esp_timer_get_time() / 1000;
    if (status) {
        event.text = status;
    }
    EventBus::GetInstance().Emit(event);
}

// ========== 用户交互事件 ==========

void EventBridge::EmitUserButtonPressed() {
    UserEvent event(EventType::USER_BUTTON_PRESSED);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitUserWakeWord(const char* wake_word) {
    UserEvent event(EventType::USER_WAKE_WORD);
    event.timestamp = esp_timer_get_time() / 1000;
    if (wake_word) {
        event.wake_word = wake_word;
    }
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitUserAbort() {
    Event event(EventType::USER_ABORT);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}

// ========== 系统事件 ==========

void EventBridge::EmitSystemError(int code, const char* message, const char* category) {
    ErrorEvent event(EventType::SYSTEM_ERROR);
    event.timestamp = esp_timer_get_time() / 1000;
    event.code = code;
    if (message) {
        event.message = message;
    }
    if (category) {
        event.category = category;
    }
    EventBus::GetInstance().Emit(event);
}

void EventBridge::EmitSystemIdleTimeout() {
    Event event(EventType::SYSTEM_IDLE_TIMEOUT);
    event.timestamp = esp_timer_get_time() / 1000;
    EventBus::GetInstance().Emit(event);
}
