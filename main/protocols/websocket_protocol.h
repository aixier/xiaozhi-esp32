/**
 * @file websocket_protocol.h
 * @brief PSM-ESP32-CNV-001: CNV-C001 WebSocketProtocol WebSocket协议实现
 * @trace PIM-CNV-001 对话域需求规格
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>
#include <vector>

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_;
    std::unique_ptr<WebSocket> websocket_;
    int version_ = 3;  // 使用 BinaryProtocol3，与服务器匹配

    // 调试统计: 接收帧计数
    uint32_t rx_frame_count_ = 0;
    uint32_t rx_total_bytes_ = 0;
    std::vector<uint16_t> rx_frame_sizes_;  // 记录前N帧大小用于对比

    // WebSocket 心跳保活 (4G 运营商 NAT 超时约 10-30 秒)
    esp_timer_handle_t heartbeat_timer_ = nullptr;
    static const int HEARTBEAT_INTERVAL_MS = 8000;  // 8 秒发送一次 ping
    bool audio_streaming_ = false;  // 音频流传输中时暂停心跳

    void StartHeartbeat();
    void StopHeartbeat();
    void OnHeartbeatTimer();
    void SetAudioStreaming(bool streaming) { audio_streaming_ = streaming; }

    void ParseServerHello(const cJSON* root);
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
};

#endif
