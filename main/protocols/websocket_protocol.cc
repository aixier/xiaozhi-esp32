#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"
#include "emotion/emotion_downloader.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();

    // 创建心跳定时器
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            static_cast<WebsocketProtocol*>(arg)->OnHeartbeatTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws_heartbeat",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&timer_args, &heartbeat_timer_);
}

WebsocketProtocol::~WebsocketProtocol() {
    StopHeartbeat();
    if (heartbeat_timer_) {
        esp_timer_delete(heartbeat_timer_);
        heartbeat_timer_ = nullptr;
    }
    vEventGroupDelete(event_group_handle_);
}

void WebsocketProtocol::StartHeartbeat() {
    if (heartbeat_timer_) {
        esp_timer_start_periodic(heartbeat_timer_, HEARTBEAT_INTERVAL_MS * 1000);
        ESP_LOGI(TAG, "WebSocket heartbeat started (interval: %dms)", HEARTBEAT_INTERVAL_MS);
    }
}

void WebsocketProtocol::StopHeartbeat() {
    if (heartbeat_timer_) {
        esp_timer_stop(heartbeat_timer_);
        ESP_LOGD(TAG, "WebSocket heartbeat stopped");
    }
}

void WebsocketProtocol::OnHeartbeatTimer() {
    // 音频流传输中时暂停心跳，避免 AT+MIPSEND 阻塞 URC 接收导致丢包
    if (audio_streaming_) {
        ESP_LOGD(TAG, "Skipping heartbeat during audio streaming");
        return;
    }
    if (websocket_ && websocket_->IsConnected()) {
        websocket_->Ping();
        ESP_LOGD(TAG, "Sent WebSocket ping");
    }
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else {
        return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
    }
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    StopHeartbeat();
    audio_streaming_ = false;  // 重置标志，确保下次连接心跳正常
    websocket_.reset();
}

bool WebsocketProtocol::OpenAudioChannel() {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    error_occurred_ = false;
    audio_streaming_ = false;  // 重置音频流标志
    // 重要：初始化 last_incoming_time_ 防止超时误判
    last_incoming_time_ = std::chrono::steady_clock::now();
    ESP_LOGI(TAG, "OpenAudioChannel: url=%s, version=%d", url.c_str(), version_);

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        // 使用 INFO 级别日志方便调试
        // 高频日志改为 LOGD，避免影响音频数据处理性能
        ESP_LOGD(TAG, "OnData: len=%d, binary=%d", (int)len, binary);
        if (binary) {
            if (version_ == 3) {
                // 先打印消息类型
                BinaryProtocol3* bp3_peek = (BinaryProtocol3*)data;
                ESP_LOGD(TAG, "Binary msg_type=0x%02X, payload_size=%d", bp3_peek->type, ntohs(bp3_peek->payload_size));
            }
            if (version_ == 2) {
                if (on_incoming_audio_ != nullptr) {
                    BinaryProtocol2* bp2 = (BinaryProtocol2*)data;
                    bp2->version = ntohs(bp2->version);
                    bp2->type = ntohs(bp2->type);
                    bp2->timestamp = ntohl(bp2->timestamp);
                    bp2->payload_size = ntohl(bp2->payload_size);
                    auto payload = (uint8_t*)bp2->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = bp2->timestamp,
                        .payload = std::vector<uint8_t>(payload, payload + bp2->payload_size)
                    }));
                }
            } else if (version_ == 3) {
                // BinaryProtocol3: type(1) + reserved(1) + payload_size(2) + payload
                BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
                uint8_t msg_type = bp3->type;
                uint16_t payload_size = ntohs(bp3->payload_size);
                auto payload = (uint8_t*)bp3->payload;

                // 消息类型定义 (与服务器 MessageType 一致)
                // 0x10: AUDIO_START, 0x11: AUDIO_DATA, 0x12: AUDIO_END
                // 0x20: TEXT_ASR, 0x21: TEXT_LLM, 0x22: TEXT_TTS
                // 0x0F: ERROR

                if (msg_type == 0x11) {
                    // AUDIO_DATA: 音频数据 - 统计帧信息
                    rx_frame_count_++;
                    rx_total_bytes_ += payload_size;

                    // 记录前20帧和最后10帧的大小用于对比
                    if (rx_frame_sizes_.size() < 20) {
                        rx_frame_sizes_.push_back(payload_size);
                    }
                    // 每100帧打印一次进度
                    if (rx_frame_count_ % 100 == 0) {
                        ESP_LOGI(TAG, "RX progress: %lu frames, %lu bytes",
                                 (unsigned long)rx_frame_count_, (unsigned long)rx_total_bytes_);
                    }

                    if (on_incoming_audio_ != nullptr) {
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                            .sample_rate = server_sample_rate_,
                            .frame_duration = server_frame_duration_,
                            .timestamp = 0,
                            .payload = std::vector<uint8_t>(payload, payload + payload_size)
                        }));
                    }
                } else if (msg_type == 0x12) {
                    // AUDIO_END: 音频结束 - 恢复心跳，打印完整统计
                    audio_streaming_ = false;  // 恢复心跳
                    ESP_LOGI(TAG, "=== AUDIO RX STATS (heartbeat resumed) ===");
                    ESP_LOGI(TAG, "Total frames: %lu", (unsigned long)rx_frame_count_);
                    ESP_LOGI(TAG, "Total bytes: %lu", (unsigned long)rx_total_bytes_);

                    // 打印前20帧大小签名，用于和服务器对比
                    if (!rx_frame_sizes_.empty()) {
                        std::string sig;
                        for (size_t i = 0; i < rx_frame_sizes_.size() && i < 20; i++) {
                            if (i > 0) sig += ",";
                            sig += std::to_string(rx_frame_sizes_[i]);
                        }
                        ESP_LOGI(TAG, "First 20 sizes: [%s]", sig.c_str());
                    }
                    ESP_LOGI(TAG, "======================");

                    if (on_incoming_json_ != nullptr) {
                        // 构造 tts stop 消息，与原有逻辑兼容
                        cJSON* root = cJSON_CreateObject();
                        cJSON_AddStringToObject(root, "type", "tts");
                        cJSON_AddStringToObject(root, "state", "stop");
                        on_incoming_json_(root);
                        cJSON_Delete(root);
                    }
                } else if (msg_type == 0x10) {
                    // AUDIO_START: 音频开始 - 重置帧统计，暂停心跳
                    rx_frame_count_ = 0;
                    rx_total_bytes_ = 0;
                    rx_frame_sizes_.clear();
                    audio_streaming_ = true;  // 暂停心跳，避免 ping 阻塞音频接收
                    ESP_LOGI(TAG, "Received AUDIO_START - reset frame stats, heartbeat paused");
                    if (on_incoming_json_ != nullptr) {
                        cJSON* root = cJSON_CreateObject();
                        cJSON_AddStringToObject(root, "type", "tts");
                        cJSON_AddStringToObject(root, "state", "start");
                        on_incoming_json_(root);
                        cJSON_Delete(root);
                    }
                } else if (msg_type == 0x20 || msg_type == 0x21) {
                    // TEXT_ASR (0x20) 或 TEXT_LLM (0x21): 文本消息
                    std::string json_str((char*)payload, payload_size);
                    ESP_LOGI(TAG, "Received %s: %s", msg_type == 0x20 ? "TEXT_ASR" : "TEXT_LLM", json_str.c_str());

                    if (on_incoming_json_ != nullptr) {
                        // 解析 payload 中的 JSON
                        cJSON* payload_json = cJSON_Parse(json_str.c_str());
                        if (payload_json) {
                            // 构造应用层期望的消息格式
                            cJSON* root = cJSON_CreateObject();
                            cJSON_AddStringToObject(root, "type", msg_type == 0x20 ? "stt" : "llm");

                            // 提取 text 字段
                            cJSON* text = cJSON_GetObjectItem(payload_json, "text");
                            if (cJSON_IsString(text)) {
                                cJSON_AddStringToObject(root, "text", text->valuestring);
                            }

                            // 对于 LLM 消息，检查 is_final
                            cJSON* is_final = cJSON_GetObjectItem(payload_json, "is_final");
                            if (cJSON_IsBool(is_final) && cJSON_IsTrue(is_final)) {
                                cJSON_AddBoolToObject(root, "is_final", true);
                            }

                            on_incoming_json_(root);
                            cJSON_Delete(root);
                            cJSON_Delete(payload_json);
                        }
                    }
                } else if (msg_type == 0x0F) {
                    // ERROR - 服务器发生错误 (如 ASR 超时)
                    std::string json_str((char*)payload, payload_size);
                    ESP_LOGE(TAG, "Received ERROR: %s", json_str.c_str());

                    // 重要修复：收到错误后重新发送 listen:start
                    // 否则服务器 is_listening=False，设备继续发音频会被忽略
#if CONFIG_ALWAYS_ONLINE
                    ESP_LOGI(TAG, "Always Online: error received, re-sending listen:start");
                    Application::GetInstance().Schedule([this]() {
                        // 重新发送 listen:start 让服务器恢复监听状态
                        SendStartListening(kListeningModeAutoStop);
                    });
#endif
                } else if (msg_type == 0x38) {
                    // EMOTION_UPDATE (0x38): 表情更新推送
                    std::string json_str((char*)payload, payload_size);
                    ESP_LOGI(TAG, "Received EMOTION_UPDATE: %s", json_str.c_str());
                    EmotionDownloader::GetInstance().HandleEmotionUpdate(json_str);
                } else {
                    ESP_LOGW(TAG, "Unknown binary message type: 0x%02X", msg_type);
                }
            } else {
                // version 1: 原始音频数据
                if (on_incoming_audio_ != nullptr) {
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                    }));
                }
            }
        } else {
            // 关键修复：为 JSON 数据添加 null terminator
            // WebSocket 接收的数据可能没有 null terminator，cJSON_Parse 需要它
            std::string json_str(data, len);
            ESP_LOGI(TAG, "Received JSON (%d bytes): %.100s%s", (int)len, json_str.c_str(), len > 100 ? "..." : "");

            // Parse JSON data (now with guaranteed null terminator)
            auto root = cJSON_Parse(json_str.c_str());
            if (root == nullptr) {
                const char* error_ptr = cJSON_GetErrorPtr();
                ESP_LOGE(TAG, "JSON parse error at: %s", error_ptr ? error_ptr : "unknown");
                return;
            }

            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
                ESP_LOGI(TAG, "Message type: %s", type->valuestring);
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Missing message type, data: %s", json_str.c_str());
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
        ESP_LOGD(TAG, "Updated last_incoming_time_");
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGW(TAG, "Websocket disconnected callback triggered");
        StopHeartbeat();  // 停止心跳
        audio_streaming_ = false;  // 重置音频流标志
        if (on_audio_channel_closed_ != nullptr) {
            ESP_LOGI(TAG, "Calling on_audio_channel_closed_ callback");
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }
    ESP_LOGI(TAG, "WebSocket connected successfully");

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    ESP_LOGI(TAG, "Sending client hello: %s", message.c_str());
    if (!SendText(message)) {
        ESP_LOGE(TAG, "Failed to send client hello");
        return false;
    }
    ESP_LOGI(TAG, "Client hello sent, waiting for server hello (timeout: 10s)");

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello (timeout or connection closed)");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }
    ESP_LOGI(TAG, "Server hello received, session_id=%s", session_id_.c_str());

    // 启动心跳保活 (4G 运营商 NAT 超时约 10-30 秒)
    StartHeartbeat();

    if (on_audio_channel_opened_ != nullptr) {
        ESP_LOGI(TAG, "Calling on_audio_channel_opened_ callback");
        on_audio_channel_opened_();
    }

    ESP_LOGI(TAG, "OpenAudioChannel completed successfully");
    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    ESP_LOGI(TAG, "ParseServerHello: parsing server hello message");

    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr) {
        ESP_LOGE(TAG, "ParseServerHello: transport field is NULL");
        return;
    }
    if (!cJSON_IsString(transport)) {
        ESP_LOGE(TAG, "ParseServerHello: transport is not a string");
        return;
    }
    ESP_LOGI(TAG, "ParseServerHello: transport=%s", transport->valuestring);
    if (strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "ParseServerHello: session_id=%s", session_id_.c_str());
    } else {
        ESP_LOGW(TAG, "ParseServerHello: session_id is missing or invalid");
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
            ESP_LOGI(TAG, "ParseServerHello: sample_rate=%d", server_sample_rate_);
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
            ESP_LOGI(TAG, "ParseServerHello: frame_duration=%d", server_frame_duration_);
        }
    }

    ESP_LOGI(TAG, "ParseServerHello: setting SERVER_HELLO_EVENT");
    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
