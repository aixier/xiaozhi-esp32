#include "at_scheduler.h"

#include <esp_log.h>

static const char* TAG = "AtScheduler";

AtScheduler& AtScheduler::GetInstance() {
    static AtScheduler instance;
    return instance;
}

AtScheduler::AtScheduler() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }
}

AtScheduler::~AtScheduler() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

void AtScheduler::SetExecutor(CommandExecutor executor) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        executor_ = executor;
        xSemaphoreGive(mutex_);
    }
}

void AtScheduler::BeginDataSession() {
    if (in_data_session_) {
        return;
    }

    in_data_session_ = true;
    ESP_LOGD(TAG, "Begin data session, LOW priority commands will be queued");
}

void AtScheduler::EndDataSession() {
    if (!in_data_session_) {
        return;
    }

    in_data_session_ = false;
    ESP_LOGD(TAG, "End data session, flushing %d pending commands",
             static_cast<int>(pending_commands_.size()));

    FlushPending();
}

bool AtScheduler::IsInDataSession() const {
    return in_data_session_;
}

bool AtScheduler::Execute(const std::string& cmd, Priority priority, int timeout_ms) {
    // HIGH 和 NORMAL 优先级: 立即执行
    if (priority == HIGH || priority == NORMAL) {
        if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
            CommandExecutor exec = executor_;
            xSemaphoreGive(mutex_);

            if (exec) {
                return exec(cmd, timeout_ms);
            } else {
                ESP_LOGW(TAG, "No executor set, command not executed: %s", cmd.c_str());
                return false;
            }
        }
        return false;
    }

    // LOW 优先级: 数据会话期间排队
    if (priority == LOW && in_data_session_) {
        if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
            if (pending_commands_.size() < MAX_PENDING_COMMANDS) {
                pending_commands_.push_back({cmd, timeout_ms});
                ESP_LOGD(TAG, "Command queued (data session active): %s", cmd.c_str());
                xSemaphoreGive(mutex_);
                return true;  // 表示已排队
            } else {
                ESP_LOGW(TAG, "Pending queue full, dropping command: %s", cmd.c_str());
                xSemaphoreGive(mutex_);
                return false;
            }
        }
        return false;
    }

    // LOW 优先级 + 非数据会话: 立即执行
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        CommandExecutor exec = executor_;
        xSemaphoreGive(mutex_);

        if (exec) {
            return exec(cmd, timeout_ms);
        } else {
            ESP_LOGW(TAG, "No executor set, command not executed: %s", cmd.c_str());
            return false;
        }
    }

    return false;
}

void AtScheduler::FlushPending() {
    std::vector<PendingCommand> commands;

    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        commands = std::move(pending_commands_);
        pending_commands_.clear();
        xSemaphoreGive(mutex_);
    }

    // 在锁外执行命令
    for (const auto& pc : commands) {
        ESP_LOGD(TAG, "Executing pending command: %s", pc.cmd.c_str());

        CommandExecutor exec;
        if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
            exec = executor_;
            xSemaphoreGive(mutex_);
        }

        if (exec) {
            exec(pc.cmd, pc.timeout_ms);
        }

        // 给其他任务机会运行
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int AtScheduler::GetPendingCount() const {
    int count = 0;
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        count = pending_commands_.size();
        xSemaphoreGive(mutex_);
    }
    return count;
}

void AtScheduler::ClearPending() {
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        pending_commands_.clear();
        xSemaphoreGive(mutex_);
    }
    ESP_LOGD(TAG, "Pending commands cleared");
}
