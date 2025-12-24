#include "board.h"
#include "system_info.h"
#include "settings.h"
#include "display/display.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_chip_info.h>
#include <esp_random.h>
#include <esp_mac.h>
#if CONFIG_IDF_TARGET_ESP32P4
#include <esp_wifi.h>
#endif

#define TAG "Board"

Board::Board() {
    Settings settings("board", true);

    // 始终基于 MAC 地址生成 UUID，确保 Client-Id 永远固定
    std::string mac_based_uuid = GenerateUuid();
    std::string stored_uuid = settings.GetString("uuid");

    if (stored_uuid.empty() || stored_uuid != mac_based_uuid) {
        // 首次启动或旧的随机 UUID，更新为基于 MAC 的 UUID
        uuid_ = mac_based_uuid;
        settings.SetString("uuid", uuid_);
        if (!stored_uuid.empty()) {
            ESP_LOGI(TAG, "UUID updated from %s to %s (MAC-based)",
                     stored_uuid.c_str(), uuid_.c_str());
        }
    } else {
        uuid_ = stored_uuid;
    }

    ESP_LOGI(TAG, "UUID=%s SKU=%s", uuid_.c_str(), BOARD_NAME);
}

std::string Board::GenerateUuid() {
    // 基于 MAC 地址生成固定 UUID，确保每次烧录后 Client-Id 不变
    // 格式：xxxxxxxx-xxxx-5xxx-yxxx-xxxxxxxxxxxx (UUID v5 风格)

    // 获取 MAC 地址 (6 字节)
    uint8_t mac[6];
#if CONFIG_IDF_TARGET_ESP32P4
    esp_wifi_get_mac(WIFI_IF_STA, mac);
#else
    esp_efuse_mac_get_default(mac);
#endif

    // 构建 16 字节的 UUID 数据
    // 前 10 字节：基于 MAC 地址的确定性填充
    // 后 6 字节：MAC 地址本身
    uint8_t uuid[16];

    // 使用 MAC 地址的字节异或生成前缀
    uuid[0] = mac[0] ^ mac[5];
    uuid[1] = mac[1] ^ mac[4];
    uuid[2] = mac[2] ^ mac[3];
    uuid[3] = mac[0] ^ mac[2] ^ mac[4];
    uuid[4] = mac[1] ^ mac[3] ^ mac[5];
    uuid[5] = mac[0] ^ mac[1] ^ mac[2];
    uuid[6] = 0x50 | (mac[3] & 0x0F);    // 版本 5 (基于名称)
    uuid[7] = mac[3] ^ mac[4] ^ mac[5];
    uuid[8] = 0x80 | (mac[4] & 0x3F);    // 变体 1
    uuid[9] = mac[0] ^ mac[5];

    // 后 6 字节直接使用 MAC 地址
    uuid[10] = mac[0];
    uuid[11] = mac[1];
    uuid[12] = mac[2];
    uuid[13] = mac[3];
    uuid[14] = mac[4];
    uuid[15] = mac[5];

    // 将字节转换为标准的 UUID 字符串格式
    char uuid_str[37];
    snprintf(uuid_str, sizeof(uuid_str),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);

    ESP_LOGI(TAG, "Generated UUID from MAC: %s", uuid_str);
    return std::string(uuid_str);
}

bool Board::GetBatteryLevel(int &level, bool& charging, bool& discharging) {
    return false;
}

bool Board::GetTemperature(float& esp32temp){
    return false;
}

bool Board::Gethead_value(uint32_t& head_value){
    return false;
}

bool Board::Getbody_value(uint32_t& body_value){
    return false;
}

Display* Board::GetDisplay() {
    static NoDisplay display;
    return &display;
}

Camera* Board::GetCamera() {
    return nullptr;
}

Led* Board::GetLed() {
    static NoLed led;
    return &led;
}

std::string Board::GetJson() {
    /* 
        {
            "version": 2,
            "flash_size": 4194304,
            "psram_size": 0,
            "minimum_free_heap_size": 123456,
            "mac_address": "00:00:00:00:00:00",
            "uuid": "00000000-0000-0000-0000-000000000000",
            "chip_model_name": "esp32s3",
            "chip_info": {
                "model": 1,
                "cores": 2,
                "revision": 0,
                "features": 0
            },
            "application": {
                "name": "my-app",
                "version": "1.0.0",
                "compile_time": "2021-01-01T00:00:00Z"
                "idf_version": "4.2-dev"
                "elf_sha256": ""
            },
            "partition_table": [
                "app": {
                    "label": "app",
                    "type": 1,
                    "subtype": 2,
                    "address": 0x10000,
                    "size": 0x100000
                }
            ],
            "ota": {
                "label": "ota_0"
            },
            "board": {
                ...
            }
        }
    */
    std::string json = R"({"version":2,"language":")" + std::string(Lang::CODE) + R"(",)";
    json += R"("flash_size":)" + std::to_string(SystemInfo::GetFlashSize()) + R"(,)";
    json += R"("minimum_free_heap_size":")" + std::to_string(SystemInfo::GetMinimumFreeHeapSize()) + R"(",)";
    json += R"("mac_address":")" + SystemInfo::GetMacAddress() + R"(",)";
    json += R"("uuid":")" + uuid_ + R"(",)";
    json += R"("chip_model_name":")" + SystemInfo::GetChipModelName() + R"(",)";

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    json += R"("chip_info":{)";
    json += R"("model":)" + std::to_string(chip_info.model) + R"(,)";
    json += R"("cores":)" + std::to_string(chip_info.cores) + R"(,)";
    json += R"("revision":)" + std::to_string(chip_info.revision) + R"(,)";
    json += R"("features":)" + std::to_string(chip_info.features) + R"(},)";

    auto app_desc = esp_app_get_description();
    json += R"("application":{)";
    json += R"("name":")" + std::string(app_desc->project_name) + R"(",)";
    json += R"("version":")" + std::string(app_desc->version) + R"(",)";
    json += R"("compile_time":")" + std::string(app_desc->date) + R"(T)" + std::string(app_desc->time) + R"(Z",)";
    json += R"("idf_version":")" + std::string(app_desc->idf_ver) + R"(",)";
    char sha256_str[65];
    for (int i = 0; i < 32; i++) {
        snprintf(sha256_str + i * 2, sizeof(sha256_str) - i * 2, "%02x", app_desc->app_elf_sha256[i]);
    }
    json += R"("elf_sha256":")" + std::string(sha256_str) + R"(")";
    json += R"(},)";

    json += R"("partition_table": [)";
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *partition = esp_partition_get(it);
        json += R"({)";
        json += R"("label":")" + std::string(partition->label) + R"(",)";
        json += R"("type":)" + std::to_string(partition->type) + R"(,)";
        json += R"("subtype":)" + std::to_string(partition->subtype) + R"(,)";
        json += R"("address":)" + std::to_string(partition->address) + R"(,)";
        json += R"("size":)" + std::to_string(partition->size) + R"(},)";;
        it = esp_partition_next(it);
    }
    json.pop_back(); // Remove the last comma
    json += R"(],)";

    json += R"("ota":{)";
    auto ota_partition = esp_ota_get_running_partition();
    json += R"("label":")" + std::string(ota_partition->label) + R"(")";
    json += R"(},)";

    json += R"("board":)" + GetBoardJson();

    // Close the JSON object
    json += R"(})";
    return json;
}