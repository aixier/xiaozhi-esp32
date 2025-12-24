#ifndef AT_SCHEDULER_H
#define AT_SCHEDULER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>
#include <atomic>
#include <functional>

/**
 * AT 命令调度器
 *
 * 功能:
 * - 优先级调度: 数据传输(HIGH) > 连接管理(NORMAL) > 状态查询(LOW)
 * - 数据会话保护: BeginDataSession 期间阻塞 LOW 优先级命令
 * - 命令队列: LOW 优先级命令在数据会话期间排队等待
 *
 * 使用示例:
 * ```cpp
 * auto& scheduler = AtScheduler::GetInstance();
 *
 * // WebSocket 数据传输前
 * scheduler.BeginDataSession();
 *
 * // 发送数据
 * scheduler.Execute("AT+MIPSEND=...", Priority::HIGH);
 *
 * // 数据传输结束
 * scheduler.EndDataSession();  // 此时执行积压的 LOW 命令
 *
 * // 正常查询 (会在数据会话期间排队)
 * scheduler.Execute("AT+CSQ", Priority::LOW);
 * ```
 */
class AtScheduler {
public:
    /**
     * 命令优先级
     */
    enum Priority {
        HIGH,   // MIPSEND, MIPREAD (数据传输) - 立即执行
        NORMAL, // MIPOPEN, MIPCLOSE (连接管理) - 立即执行
        LOW,    // CSQ, CCLK, CIMI (状态查询) - 数据会话期间排队
    };

    /**
     * 命令执行回调
     * @param cmd 要执行的命令
     * @param timeout_ms 超时时间
     * @return 执行结果 (true = 成功)
     */
    using CommandExecutor = std::function<bool(const std::string& cmd, int timeout_ms)>;

    /**
     * 获取单例实例
     */
    static AtScheduler& GetInstance();

    /**
     * 设置命令执行器
     * @param executor 实际执行 AT 命令的函数
     */
    void SetExecutor(CommandExecutor executor);

    /**
     * 开始数据会话
     * - 标记正在进行数据传输
     * - LOW 优先级命令将排队等待
     */
    void BeginDataSession();

    /**
     * 结束数据会话
     * - 执行积压的 LOW 优先级命令
     */
    void EndDataSession();

    /**
     * 检查是否在数据会话中
     */
    bool IsInDataSession() const;

    /**
     * 执行 AT 命令
     * @param cmd 命令字符串
     * @param priority 优先级
     * @param timeout_ms 超时时间 (默认 1000ms)
     * @return 是否成功 (HIGH/NORMAL 立即返回结果，LOW 在数据会话中返回 true 表示已排队)
     */
    bool Execute(const std::string& cmd, Priority priority, int timeout_ms = 1000);

    /**
     * 获取积压命令数量
     */
    int GetPendingCount() const;

    /**
     * 清空积压队列
     */
    void ClearPending();

    // 禁止拷贝
    AtScheduler(const AtScheduler&) = delete;
    AtScheduler& operator=(const AtScheduler&) = delete;

private:
    AtScheduler();
    ~AtScheduler();

    /**
     * 积压命令结构
     */
    struct PendingCommand {
        std::string cmd;
        int timeout_ms;
    };

    /**
     * 执行积压的命令
     */
    void FlushPending();

    CommandExecutor executor_;
    mutable SemaphoreHandle_t mutex_;
    std::atomic<bool> in_data_session_{false};
    std::vector<PendingCommand> pending_commands_;

    // 最大积压命令数
    static const int MAX_PENDING_COMMANDS = 10;
};

#endif // AT_SCHEDULER_H
