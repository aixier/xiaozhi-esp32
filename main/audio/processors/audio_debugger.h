/**
 * @file audio_debugger.h
 * @brief PSM-ESP32-MED-001: MED-D001 音频调试器
 * @trace PIM-MED-001 媒体域需求规格 - 调试支持组件
 * @version 1.0.0
 * @date 2025-12-27
 */

#ifndef AUDIO_DEBUGGER_H
#define AUDIO_DEBUGGER_H

#include <vector>
#include <cstdint>

#include <sys/socket.h>
#include <netinet/in.h>

class AudioDebugger {
public:
    AudioDebugger();
    ~AudioDebugger();

    void Feed(const std::vector<int16_t>& data);

private:
    int udp_sockfd_ = -1;
    struct sockaddr_in udp_server_addr_;
};

#endif 