/**
 * @file emotion_downloader.cc
 * @brief PSM-ESP32-MED-002: 表情资源下载管理实现
 * @trace PIM-MED-002 MED-C201
 * @version 1.0.0
 * @date 2025-12-29
 */

#include "emotion_downloader.h"
#include "emotion_storage.h"

#include <cstring>
#include <cstdio>
#include <esp_log.h>
#include <esp_http_client.h>
#include <mbedtls/md5.h>
#include <cJSON.h>

#include "application.h"
#include "core/event_bridge.h"

EmotionDownloader& EmotionDownloader::GetInstance() {
    static EmotionDownloader instance;
    return instance;
}

bool EmotionDownloader::Download(const std::string& url, const std::string& expected_md5,
                                  EmotionDownloadCallback callback) {
    ESP_LOGI(TAG, "Downloading emotion from: %s", url.c_str());

    // 分配下载缓冲区
    uint8_t* buffer = (uint8_t*)heap_caps_malloc(MAX_FILE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        if (callback) callback(false, "Memory allocation failed");
        return false;
    }

    // 执行下载
    int downloaded = HttpDownload(url, buffer, MAX_FILE_SIZE);
    if (downloaded <= 0) {
        ESP_LOGE(TAG, "Download failed");
        heap_caps_free(buffer);
        if (callback) callback(false, "Download failed");
        return false;
    }

    ESP_LOGI(TAG, "Downloaded %d bytes", downloaded);

    // 验证 MD5
    if (!expected_md5.empty()) {
        std::string actual_md5 = CalculateMd5(buffer, downloaded);
        if (actual_md5 != expected_md5) {
            ESP_LOGE(TAG, "MD5 mismatch: expected=%s, actual=%s",
                     expected_md5.c_str(), actual_md5.c_str());
            heap_caps_free(buffer);
            if (callback) callback(false, "MD5 mismatch");
            return false;
        }
        ESP_LOGI(TAG, "MD5 verified: %s", actual_md5.c_str());
    }

    // 保存到 SPIFFS
    auto& storage = EmotionStorage::GetInstance();
    bool saved = storage.Save(buffer, downloaded);
    heap_caps_free(buffer);

    if (!saved) {
        ESP_LOGE(TAG, "Failed to save emotion");
        if (callback) callback(false, "Save failed");
        return false;
    }

    ESP_LOGI(TAG, "Emotion saved successfully");
    if (callback) callback(true, "");
    return true;
}

void EmotionDownloader::HandleEmotionUpdate(const std::string& json_payload) {
    ESP_LOGI(TAG, "Handling EMOTION_UPDATE: %s", json_payload.c_str());

    // 解析 JSON
    cJSON* root = cJSON_Parse(json_payload.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    cJSON* emotion_id_json = cJSON_GetObjectItem(root, "emotion_id");
    cJSON* download_url_json = cJSON_GetObjectItem(root, "download_url");
    cJSON* md5_json = cJSON_GetObjectItem(root, "md5");

    if (!cJSON_IsNumber(emotion_id_json) || !cJSON_IsString(download_url_json)) {
        ESP_LOGE(TAG, "Invalid JSON format");
        cJSON_Delete(root);
        return;
    }

    int emotion_id = emotion_id_json->valueint;
    std::string download_url = download_url_json->valuestring;
    std::string md5 = cJSON_IsString(md5_json) ? md5_json->valuestring : "";

    cJSON_Delete(root);

    // 设置当前 emotion_id
    current_emotion_id_ = emotion_id;

    // 发送 downloading 状态
    SendAck(emotion_id, "downloading");

    // 异步下载 (在主循环中调度)
    Application::GetInstance().Schedule([this, download_url, md5, emotion_id]() {
        bool success = Download(download_url, md5, [this, emotion_id](bool ok, const std::string& error) {
            if (ok) {
                SendAck(emotion_id, "success");
                // 通知显示模块刷新表情为自定义表情
                ESP_LOGI(TAG, "Emotion downloaded, switching to custom emotion");
                EventBridge::EmitSetEmotion("custom");
            } else {
                SendAck(emotion_id, "failed", error);
            }
        });

        if (!success) {
            SendAck(emotion_id, "failed", "Download initiation failed");
        }
    });
}

void EmotionDownloader::SendAck(int emotion_id, const std::string& status, const std::string& error) {
    ESP_LOGI(TAG, "Sending EMOTION_ACK: id=%d, status=%s", emotion_id, status.c_str());

    // 构造 JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "emotion_id", emotion_id);
    cJSON_AddStringToObject(root, "status", status.c_str());
    if (!error.empty()) {
        cJSON_AddStringToObject(root, "error", error.c_str());
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == nullptr) {
        ESP_LOGE(TAG, "Failed to create JSON");
        return;
    }

    // TODO: 发送 WebSocket 二进制消息 (type=0x39 EMOTION_ACK)
    // 需要在 Protocol 添加 SendBinary 方法后完善
    ESP_LOGI(TAG, "EMOTION_ACK: %s", json_str);

    cJSON_free(json_str);
}

int EmotionDownloader::HttpDownload(const std::string& url, uint8_t* buffer, size_t max_size) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = DOWNLOAD_TIMEOUT_MS;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch headers");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    if ((size_t)content_length > max_size) {
        ESP_LOGE(TAG, "Content too large: %d > %zu", content_length, max_size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return -1;
    }

    int total_read = 0;
    while (total_read < content_length) {
        int read = esp_http_client_read(client, (char*)(buffer + total_read),
                                         content_length - total_read);
        if (read < 0) {
            ESP_LOGE(TAG, "Read error");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }
        if (read == 0) {
            break;  // EOF
        }
        total_read += read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return total_read;
}

std::string EmotionDownloader::CalculateMd5(const uint8_t* data, size_t size) {
    unsigned char digest[16];
    mbedtls_md5_context ctx;

    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, data, size);
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    char hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[32] = '\0';

    return std::string(hex);
}
