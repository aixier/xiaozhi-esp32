/**
 * @file emotion_storage.cc
 * @brief PSM-ESP32-MED-002: 表情资源存储管理实现
 * @trace PIM-MED-002 MED-C202
 * @version 1.0.0
 * @date 2025-12-29
 */

#include "emotion_storage.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_log.h>
#include <esp_spiffs.h>

EmotionStorage& EmotionStorage::GetInstance() {
    static EmotionStorage instance;
    return instance;
}

bool EmotionStorage::Save(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        ESP_LOGE(TAG, "Invalid data");
        return false;
    }

    // 验证 GIF 格式
    if (size < 6 || (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
        ESP_LOGE(TAG, "Invalid GIF format");
        return false;
    }

    // 删除旧表情
    Delete();

    // 写入新文件
    FILE* file = fopen(EMOTION_PATH, "wb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", EMOTION_PATH);
        return false;
    }

    size_t written = fwrite(data, 1, size, file);
    fclose(file);

    if (written != size) {
        ESP_LOGE(TAG, "Write incomplete: %zu / %zu", written, size);
        Delete();  // 清理不完整的文件
        return false;
    }

    ESP_LOGI(TAG, "Saved custom emotion: %zu bytes", size);
    return true;
}

bool EmotionStorage::Delete() {
    if (!HasCustomEmotion()) {
        return true;  // 文件不存在，视为成功
    }

    if (unlink(EMOTION_PATH) != 0) {
        ESP_LOGE(TAG, "Failed to delete: %s", EMOTION_PATH);
        return false;
    }

    ESP_LOGI(TAG, "Deleted custom emotion");
    return true;
}

bool EmotionStorage::HasCustomEmotion() {
    struct stat st;
    return (stat(EMOTION_PATH, &st) == 0);
}

size_t EmotionStorage::GetFreeSpace() {
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info: %s", esp_err_to_name(ret));
        return 0;
    }
    return total - used;
}
