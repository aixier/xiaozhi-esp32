/**
 * @file emotion_storage.h
 * @brief PSM-ESP32-MED-002: 表情资源存储管理
 * @trace PIM-MED-002 MED-C202
 * @version 1.0.0
 * @date 2025-12-29
 */

#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

/**
 * 表情资源存储管理
 *
 * 负责 SPIFFS 存储管理，只保留一个自定义表情
 */
class EmotionStorage {
public:
    static EmotionStorage& GetInstance();

    /**
     * 保存表情文件 (覆盖旧的)
     * @param data GIF 数据
     * @param size 数据大小
     * @return 是否成功
     */
    bool Save(const uint8_t* data, size_t size);

    /**
     * 删除自定义表情
     * @return 是否成功
     */
    bool Delete();

    /**
     * 检查是否存在自定义表情
     * @return 是否存在
     */
    bool HasCustomEmotion();

    /**
     * 获取存储路径
     * @return SPIFFS 路径
     */
    const char* GetPath() const { return EMOTION_PATH; }

    /**
     * 获取 LVGL 路径
     * @return LVGL 文件路径 (带 S: 前缀)
     */
    const char* GetLvglPath() const { return LVGL_PATH; }

    /**
     * 获取 SPIFFS 可用空间
     * @return 可用字节数
     */
    size_t GetFreeSpace();

private:
    EmotionStorage() = default;
    EmotionStorage(const EmotionStorage&) = delete;
    EmotionStorage& operator=(const EmotionStorage&) = delete;

    static constexpr const char* TAG = "EmotionStorage";
    static constexpr const char* EMOTION_PATH = "/spiffs/custom_emotion.gif";
    static constexpr const char* LVGL_PATH = "S:/custom_emotion.gif";
};
