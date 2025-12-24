#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "event_types.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

/**
 * 事件总线 - 发布/订阅模式的事件分发系统
 *
 * 特性:
 * - 同步发送: Emit() 立即在当前任务中执行所有处理器
 * - 异步发送: EmitAsync() 将事件放入队列，由事件循环处理
 * - 线程安全: 所有操作都是线程安全的
 * - 优先级: 支持处理器优先级 (高优先级先执行)
 *
 * 使用示例:
 * ```cpp
 * // 订阅事件
 * auto& bus = EventBus::GetInstance();
 * int id = bus.Subscribe(EventType::AUDIO_OUTPUT_START, [](const Event& e) {
 *     // 处理音频开始事件
 * });
 *
 * // 发送事件
 * AudioDataEvent event(EventType::AUDIO_OUTPUT_DATA);
 * event.data = audio_data;
 * bus.Emit(event);
 *
 * // 取消订阅
 * bus.Unsubscribe(EventType::AUDIO_OUTPUT_START, id);
 * ```
 */
class EventBus {
public:
    /**
     * 处理器优先级
     */
    enum Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
    };

    /**
     * 获取单例实例
     */
    static EventBus& GetInstance();

    /**
     * 订阅事件
     *
     * @param type 事件类型
     * @param handler 事件处理器
     * @param priority 优先级 (默认 NORMAL)
     * @return 订阅 ID (用于取消订阅)
     */
    int Subscribe(EventType type, EventHandler handler, Priority priority = NORMAL);

    /**
     * 取消订阅
     *
     * @param type 事件类型
     * @param handler_id 订阅 ID
     */
    void Unsubscribe(EventType type, int handler_id);

    /**
     * 同步发送事件 (在当前任务中立即执行所有处理器)
     *
     * @param event 事件对象
     */
    void Emit(const Event& event);

    /**
     * 异步发送事件 (放入队列，由事件循环处理)
     *
     * @param event 事件对象
     * @return 是否成功放入队列
     */
    bool EmitAsync(const Event& event);

    /**
     * 启动事件循环任务 (处理异步事件)
     */
    void StartEventLoop();

    /**
     * 停止事件循环任务
     */
    void StopEventLoop();

    /**
     * 处理一个异步事件 (手动调用，用于集成到现有事件循环)
     *
     * @param timeout_ms 等待超时 (0 = 不等待)
     * @return 是否处理了事件
     */
    bool ProcessOne(int timeout_ms = 0);

    /**
     * 获取订阅者数量
     */
    int GetSubscriberCount(EventType type) const;

    /**
     * 获取异步队列中的事件数量
     */
    int GetQueuedEventCount() const;

    // 禁止拷贝
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

private:
    EventBus();
    ~EventBus();

    /**
     * 订阅者信息
     */
    struct Subscriber {
        int id;
        EventHandler handler;
        Priority priority;
    };

    /**
     * 序列化事件用于异步队列
     */
    struct QueuedEvent {
        EventType type;
        uint32_t timestamp;
        // 简化: 仅支持基本事件数据
        int error_code;
        char message[128];
        char emotion[32];
        char text[256];
    };

    void EventLoopTask();

    // 订阅者映射: EventType -> vector<Subscriber>
    std::map<EventType, std::vector<Subscriber>> subscribers_;

    // 保护订阅者列表的互斥锁
    mutable SemaphoreHandle_t mutex_;

    // 异步事件队列
    QueueHandle_t event_queue_;

    // 事件循环任务句柄
    TaskHandle_t event_loop_task_ = nullptr;

    // 下一个订阅者 ID
    std::atomic<int> next_id_{1};

    // 事件循环运行标志
    std::atomic<bool> running_{false};

    // 队列大小
    static const int EVENT_QUEUE_SIZE = 32;
};

#endif // EVENT_BUS_H
