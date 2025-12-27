/**
 * @file event_bridge.h
 * @brief PSM-ESP32-CNV-001: CNV-C011 EventBridge 事件桥接器
 * @trace PIM-CNV-001 对话域需求规格
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef EVENT_BRIDGE_H
#define EVENT_BRIDGE_H

#include "event_bus.h"
#include "event_types.h"

/**
 * 事件桥接器
 *
 * 功能:
 * - 连接现有 Application 代码与新的事件系统
 * - 提供简单的发布事件接口
 * - 渐进式迁移，不破坏现有功能
 *
 * 使用示例:
 * ```cpp
 * // 在现有代码中发布事件
 * EventBridge::EmitConnectionStart();
 * EventBridge::EmitConnectionSuccess();
 * EventBridge::EmitAudioStart();
 * EventBridge::EmitAudioData(data, len, duration_ms);
 * EventBridge::EmitAudioEnd();
 * ```
 */
class EventBridge {
public:
    // ========== 连接事件 ==========

    /**
     * 发布连接开始事件
     */
    static void EmitConnectionStart();

    /**
     * 发布连接成功事件
     */
    static void EmitConnectionSuccess();

    /**
     * 发布连接失败事件
     */
    static void EmitConnectionFailed(int error_code = 0, const char* message = "");

    /**
     * 发布连接断开事件
     */
    static void EmitConnectionDisconnected();

    /**
     * 发布重连中事件
     */
    static void EmitConnectionReconnecting(int retry_count = 0);

    // ========== 音频事件 ==========

    /**
     * 发布音频开始事件
     */
    static void EmitAudioOutputStart();

    /**
     * 发布音频数据事件
     */
    static void EmitAudioOutputData(const uint8_t* data, size_t len, int duration_ms = 60);

    /**
     * 发布音频结束事件
     */
    static void EmitAudioOutputEnd();

    /**
     * 发布用户语音开始事件
     */
    static void EmitAudioInputStart();

    /**
     * 发布用户语音结束事件
     */
    static void EmitAudioInputEnd();

    // ========== 显示事件 ==========

    /**
     * 发布设置表情事件
     */
    static void EmitSetEmotion(const char* emotion);

    /**
     * 发布设置文本事件
     */
    static void EmitSetText(const char* text, const char* role = "assistant");

    /**
     * 发布设置状态栏事件
     */
    static void EmitSetStatus(const char* status);

    // ========== 用户交互事件 ==========

    /**
     * 发布按钮按下事件
     */
    static void EmitUserButtonPressed();

    /**
     * 发布唤醒词检测事件
     */
    static void EmitUserWakeWord(const char* wake_word = "");

    /**
     * 发布用户中断事件
     */
    static void EmitUserAbort();

    // ========== 系统事件 ==========

    /**
     * 发布系统错误事件
     */
    static void EmitSystemError(int code, const char* message, const char* category = "system");

    /**
     * 发布空闲超时事件
     */
    static void EmitSystemIdleTimeout();
};

#endif // EVENT_BRIDGE_H
