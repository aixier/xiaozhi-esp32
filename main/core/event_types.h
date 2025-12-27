/**
 * @file event_types.h
 * @brief PSM-ESP32-CNV-001: CNV-T EventType 事件类型定义
 * @trace PIM-CNV-001 对话域需求规格
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

/**
 * 事件类型定义
 *
 * 命名规范: CATEGORY_ACTION
 * - USER_*    : 用户交互事件
 * - CONN_*    : 连接状态事件
 * - AUDIO_*   : 音频播放事件
 * - DISPLAY_* : 显示更新事件
 * - SYSTEM_*  : 系统事件
 */
enum class EventType {
    // ========== 用户交互事件 ==========
    USER_BUTTON_PRESSED,      // Boot 按钮按下
    USER_TOUCH_HEAD,          // 触摸头部
    USER_TOUCH_CHIN,          // 触摸下巴
    USER_TOUCH_LEFT,          // 触摸左脸
    USER_TOUCH_RIGHT,         // 触摸右脸
    USER_WAKE_WORD,           // 唤醒词检测到
    USER_ABORT,               // 用户中断 (再次按钮)

    // ========== 连接状态事件 ==========
    CONN_STARTING,            // 开始连接
    CONN_SUCCESS,             // 连接成功
    CONN_FAILED,              // 连接失败
    CONN_DISCONNECTED,        // 连接断开
    CONN_RECONNECTING,        // 正在重连
    CONN_HEARTBEAT_TIMEOUT,   // 心跳超时

    // ========== 音频事件 ==========
    AUDIO_INPUT_START,        // 用户语音输入开始
    AUDIO_INPUT_END,          // 用户语音输入结束
    AUDIO_INPUT_VAD,          // VAD 状态变化
    AUDIO_OUTPUT_START,       // TTS 音频开始 (收到 AUDIO_START)
    AUDIO_OUTPUT_DATA,        // TTS 音频数据 (收到 AUDIO_DATA)
    AUDIO_OUTPUT_END,         // TTS 音频结束 (收到 AUDIO_END)
    AUDIO_PLAYBACK_STARTED,   // 播放实际开始 (缓冲完成)
    AUDIO_PLAYBACK_COMPLETE,  // 播放完成 (队列清空)
    AUDIO_BUFFER_LOW,         // 缓冲区低水位警告

    // ========== 显示事件 ==========
    DISPLAY_SET_EMOTION,      // 设置表情
    DISPLAY_SET_TEXT,         // 设置文本 (对话内容)
    DISPLAY_SET_STATUS,       // 设置状态栏文本
    DISPLAY_POWER_SAVE,       // 进入/退出省电模式

    // ========== 系统事件 ==========
    SYSTEM_ERROR,             // 系统错误
    SYSTEM_IDLE_TIMEOUT,      // 空闲超时
    SYSTEM_LOW_BATTERY,       // 低电量
    SYSTEM_REBOOT,            // 重启请求

    EVENT_TYPE_MAX
};

/**
 * 事件基类
 */
struct Event {
    EventType type;
    uint32_t timestamp;  // 事件时间戳 (ms)

    Event(EventType t) : type(t), timestamp(0) {}
    virtual ~Event() = default;
};

/**
 * 用户交互事件
 */
struct UserEvent : Event {
    std::string wake_word;      // 唤醒词 (仅 USER_WAKE_WORD)
    std::string touch_prompt;   // 触摸提示语

    UserEvent(EventType t) : Event(t) {}
};

/**
 * 连接事件
 */
struct ConnectionEvent : Event {
    int error_code = 0;
    std::string error_message;
    int retry_count = 0;

    ConnectionEvent(EventType t) : Event(t) {}
};

/**
 * 音频数据事件
 */
struct AudioDataEvent : Event {
    std::vector<uint8_t> data;
    uint32_t sequence = 0;
    int duration_ms = 0;

    AudioDataEvent(EventType t) : Event(t) {}
};

/**
 * 显示事件
 */
struct DisplayEvent : Event {
    std::string emotion;        // 表情名称
    std::string text;           // 文本内容
    std::string role;           // 角色 (user/assistant/system)
    bool power_save = false;    // 省电模式开关

    DisplayEvent(EventType t) : Event(t) {}
};

/**
 * 错误事件
 */
struct ErrorEvent : Event {
    int code = 0;
    std::string message;
    std::string category;       // network/audio/system

    ErrorEvent(EventType t = EventType::SYSTEM_ERROR) : Event(t) {}
};

/**
 * 事件处理器类型
 */
using EventHandler = std::function<void(const Event&)>;

#endif // EVENT_TYPES_H
