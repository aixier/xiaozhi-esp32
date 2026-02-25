/**
 * @file emotion_downloader.h
 * @brief PSM-ESP32-MED-002: 表情资源下载管理
 * @trace PIM-MED-002 MED-C201
 * @version 1.0.0
 * @date 2025-12-29
 */

#pragma once

#include <string>
#include <cstdint>
#include <functional>

/**
 * 表情下载完成回调
 * @param success 是否成功
 * @param error_msg 错误信息 (失败时)
 */
using EmotionDownloadCallback = std::function<void(bool success, const std::string& error_msg)>;

/**
 * 表情资源下载管理
 *
 * 负责从服务器下载 GIF 表情并保存到 SPIFFS
 */
class EmotionDownloader {
public:
    static EmotionDownloader& GetInstance();

    /**
     * 下载表情资源
     * @param url 下载 URL
     * @param expected_md5 期望的 MD5 校验和
     * @param callback 完成回调
     * @return 是否开始下载
     */
    bool Download(const std::string& url, const std::string& expected_md5,
                  EmotionDownloadCallback callback = nullptr);

    /**
     * 处理 EMOTION_UPDATE 消息
     * @param json_payload JSON 负载
     */
    void HandleEmotionUpdate(const std::string& json_payload);

    /**
     * 发送表情确认消息
     * @param emotion_id 表情 ID
     * @param status 状态: success/failed/downloading
     * @param error 错误信息
     */
    void SendAck(int emotion_id, const std::string& status, const std::string& error = "");

    /**
     * 设置当前下载的表情 ID (用于发送 ACK)
     */
    void SetCurrentEmotionId(int id) { current_emotion_id_ = id; }

private:
    EmotionDownloader() = default;
    EmotionDownloader(const EmotionDownloader&) = delete;
    EmotionDownloader& operator=(const EmotionDownloader&) = delete;

    /**
     * 执行 HTTP 下载
     * @param url 下载 URL
     * @param buffer 输出缓冲区
     * @param max_size 最大大小
     * @return 下载的字节数, -1 表示失败
     */
    int HttpDownload(const std::string& url, uint8_t* buffer, size_t max_size);

    /**
     * 计算 MD5 校验和
     * @param data 数据
     * @param size 大小
     * @return MD5 字符串
     */
    std::string CalculateMd5(const uint8_t* data, size_t size);

    static constexpr const char* TAG = "EmotionDownloader";
    static constexpr size_t MAX_FILE_SIZE = 300 * 1024;  // 300KB
    static constexpr int DOWNLOAD_TIMEOUT_MS = 30000;

    int current_emotion_id_ = 0;
};
