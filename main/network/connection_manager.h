#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "core/event_bus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <esp_timer.h>

#include <string>
#include <functional>
#include <atomic>

/**
 * 连接管理器
 *
 * 功能:
 * - 统一管理 WebSocket 连接状态
 * - 心跳保活 (Ping/Pong)
 * - 断线自动重连 (指数退避)
 * - AT 命令调度
 *
 * 状态机:
 * ```
 *     DISCONNECTED
 *         │
 *     Connect()
 *         ▼
 *     CONNECTING ──────────────────┐
 *         │                        │
 *      success                   fail
 *         ▼                        ▼
 *     CONNECTED               RECONNECTING
 *         │                        │
 *   disconnect/timeout         retry < max
 *         │                        │
 *         └───────▶ RECONNECTING ──┘
 *                        │
 *                   retry >= max
 *                        ▼
 *                   DISCONNECTED
 * ```
 */
class ConnectionManager {
public:
    /**
     * 连接状态
     */
    enum State {
        DISCONNECTED,   // 未连接
        CONNECTING,     // 正在连接
        CONNECTED,      // 已连接
        RECONNECTING,   // 正在重连
    };

    /**
     * 连接回调
     */
    struct Callbacks {
        std::function<bool()> on_connect;           // 执行连接
        std::function<void()> on_disconnect;        // 执行断开
        std::function<void()> on_send_ping;         // 发送 Ping
    };

    /**
     * 获取单例实例
     */
    static ConnectionManager& GetInstance();

    /**
     * 初始化
     * @param callbacks 连接回调
     */
    void Initialize(const Callbacks& callbacks);

    /**
     * 发起连接
     */
    void Connect();

    /**
     * 主动断开
     */
    void Disconnect();

    /**
     * 获取当前状态
     */
    State GetState() const;

    /**
     * 通知连接成功
     * 由 WebSocket 协议层调用
     */
    void OnConnected();

    /**
     * 通知连接断开
     * 由 WebSocket 协议层调用
     */
    void OnDisconnected();

    /**
     * 通知收到 Pong
     * 由 WebSocket 协议层调用
     */
    void OnPongReceived();

    /**
     * 通知发生错误
     */
    void OnError(int code, const std::string& message);

    /**
     * 获取重连次数
     */
    int GetReconnectCount() const;

    // 禁止拷贝
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

private:
    ConnectionManager();
    ~ConnectionManager();

    /**
     * 启动心跳定时器
     */
    void StartHeartbeat();

    /**
     * 停止心跳定时器
     */
    void StopHeartbeat();

    /**
     * 心跳定时器回调
     */
    void OnHeartbeatTimer();

    /**
     * 尝试重连
     */
    void AttemptReconnect();

    /**
     * 重连定时器回调
     */
    void OnReconnectTimer();

    /**
     * 计算重连延迟 (指数退避)
     */
    int GetReconnectDelay() const;

    /**
     * 设置状态并发布事件
     */
    void SetState(State new_state);

    // 心跳配置
    static const int HEARTBEAT_INTERVAL_MS = 30000;  // 30秒
    static const int HEARTBEAT_TIMEOUT_MS = 10000;   // 10秒超时

    // 重连配置
    static const int RECONNECT_DELAY_INITIAL_MS = 1000;   // 首次 1 秒
    static const int RECONNECT_DELAY_MAX_MS = 30000;      // 最大 30 秒
    static const int RECONNECT_MAX_ATTEMPTS = 5;          // 最多 5 次

    State state_ = DISCONNECTED;
    Callbacks callbacks_;

    // 心跳相关
    esp_timer_handle_t heartbeat_timer_ = nullptr;
    std::atomic<bool> pong_received_{false};
    std::atomic<int64_t> last_pong_time_{0};

    // 重连相关
    esp_timer_handle_t reconnect_timer_ = nullptr;
    std::atomic<int> reconnect_count_{0};
    bool user_disconnected_ = false;

    mutable SemaphoreHandle_t mutex_;
};

#endif // CONNECTION_MANAGER_H
