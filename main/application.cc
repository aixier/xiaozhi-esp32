#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "core/event_bridge.h"
#include "settings.h"

#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

// 简短状态标识，用于 AT 命令日志
static const char* const STATE_SHORT[] = {
    "?",   // unknown
    "ST",  // starting
    "CF",  // configuring
    "I",   // idle
    "C",   // connecting
    "L",   // listening  ← 关键状态
    "S",   // speaking   ← 关键状态
    "U",   // upgrading
    "A",   // activating
    "T",   // audio_testing
    "E",   // fatal_error
    "X"    // invalid
};

// 实现 AtUart 弱符号，供 AT 命令日志使用
// 这样可以在日志中看到 AT 命令发送时的设备状态
extern "C" const char* AtUart_GetDeviceStateString() {
    auto& app = Application::GetInstance();
    int state = static_cast<int>(app.GetDeviceState());
    if (state >= 0 && state < (int)(sizeof(STATE_SHORT) / sizeof(STATE_SHORT[0]))) {
        return STATE_SHORT[state];
    }
    return "?";
}

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

#if CONFIG_ALWAYS_ONLINE
    // 创建重连定时器 (持久的，不会内存泄漏)
    esp_timer_create_args_t reconnect_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnReconnectTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "reconnect_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&reconnect_timer_args, &reconnect_timer_);
#endif
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
#if CONFIG_ALWAYS_ONLINE
    if (reconnect_timer_ != nullptr) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
    }
#endif
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);
            
            display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            board.SetPowerSaveMode(false);
            audio_service_.Stop();
            vTaskDelay(pdMS_TO_TICKS(1000));

            bool upgrade_success = ota.StartUpgrade([display](int progress, size_t speed) {
                std::thread([display, progress, speed]() {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                    display->SetChatMessage("system", buffer);
                }).detach();
            });

            if (!upgrade_success) {
                // Upgrade failed, restart audio service and continue running
                ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
                audio_service_.Start(); // Restart audio service
                board.SetPowerSaveMode(true); // Restore power save mode
                Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "sad", Lang::Sounds::P3_EXCLAMATION);
                vTaskDelay(pdMS_TO_TICKS(3000));
                // Continue to normal operation (don't break, just fall through)
            } else {
                // Upgrade success, reboot immediately
                ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
                display->SetChatMessage("system", "Upgrade successful, rebooting...");
                vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
                Reboot();
                return; // This line will never be reached after reboot
            }
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    // 使用 Alert 模式：表情 + 文字叠加显示
    display->SetAlert(emotion, message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        // 恢复到表情模式
        display->SetDisplayMode(kDisplayModeEmotion);
        EventBridge::EmitSetEmotion("neutral");
    }
}

void Application::ToggleChatState() {
    ESP_LOGI(TAG, "[ToggleChatState] >> Enter, state=%d", device_state_);
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        ESP_LOGI(TAG, "[ToggleChatState] << Exit (Activating->Idle)");
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        ESP_LOGI(TAG, "[ToggleChatState] << Exit (WifiConfiguring->AudioTesting)");
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        ESP_LOGI(TAG, "[ToggleChatState] << Exit (AudioTesting->WifiConfiguring)");
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "[ToggleChatState] Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        ESP_LOGI(TAG, "[ToggleChatState] Idle, scheduling connection...");
        Schedule([this]() {
            ESP_LOGI(TAG, "[ToggleChatState:Schedule] >> Executing in main loop");
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                ESP_LOGI(TAG, "[ToggleChatState:Schedule] Opening audio channel...");
                if (!protocol_->OpenAudioChannel()) {
                    ESP_LOGE(TAG, "[ToggleChatState:Schedule] OpenAudioChannel failed");
                    return;
                }
                ESP_LOGI(TAG, "[ToggleChatState:Schedule] Audio channel opened");
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
            ESP_LOGI(TAG, "[ToggleChatState:Schedule] << Done");
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "[ToggleChatState] Speaking, scheduling abort...");
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        ESP_LOGI(TAG, "[ToggleChatState] Listening, scheduling stop...");
        // 停止监听但保持连接，等待服务器的 LLM/TTS 响应
        Schedule([this]() {
            protocol_->SendStopListening();
#if CONFIG_ALWAYS_ONLINE
            // Always Online 模式：保持监听状态
            ESP_LOGI(TAG, "Always Online: toggle - stay in listening mode");
#else
            SetDeviceState(kDeviceStateIdle);
#endif
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
#if CONFIG_ALWAYS_ONLINE
            // Always Online 模式：保持监听状态，等待服务器响应后继续监听
            // 不切换到 Idle，保持在 Listening 状态
            ESP_LOGI(TAG, "Always Online: stop listening but stay in listening mode");
#else
            SetDeviceState(kDeviceStateIdle);
#endif
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Initialize the display engine with emotion transitions */
    DisplayEngine::Callbacks display_cbs;
    display_cbs.set_emotion = [display](const std::string& emotion) {
        display->SetEmotion(emotion.c_str());
    };
    display_cbs.set_brightness = [](int brightness) {
        // TODO: 实现亮度控制
        ESP_LOGD(TAG, "Set brightness: %d", brightness);
    };
    display_cbs.set_status = [display](const std::string& status) {
        display->SetStatus(status.c_str());
    };
    display_engine_.SetCallbacks(display_cbs);
    display_engine_.Initialize(display);
    ESP_LOGI(TAG, "DisplayEngine initialized with emotion transitions");

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    
    // Initialize volume
    Settings settings("audio", false);
    int volume = settings.GetInt("volume", 70);
    codec->SetOutputVolume(volume);
    
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    callbacks.on_playback_idle = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_PLAYBACK_IDLE);
    };
    audio_service_.SetCallbacks(callbacks);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    McpServer::GetInstance().AddCommonTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        // 接受 Speaking, Idle, 或 Listening 状态的音频
        // Listening/Idle 状态：刚收到 AUDIO_START 但 Schedule 还没执行完
        if (device_state_ == kDeviceStateSpeaking ||
            device_state_ == kDeviceStateIdle ||
            device_state_ == kDeviceStateListening) {
            // 如果不是 Speaking 状态，立即切换
            if (device_state_ != kDeviceStateSpeaking) {
                SetDeviceState(kDeviceStateSpeaking);
            }
            // 4G 最佳实践：使用非阻塞模式，队列满时丢包，避免 URC 线程阻塞导致队列溢出
            // 队列已有 200 包（12秒缓冲），偶尔丢包不影响播放
            audio_service_.PushPacketToDecodeQueue(std::move(packet), false);
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
#if CONFIG_ALWAYS_ONLINE
        // 连接成功，停止重连定时器
        StopReconnectTimer();
#endif
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
#if CONFIG_ALWAYS_ONLINE
            // Always Online 模式：断开后启动重连定时器
            ESP_LOGI(TAG, "Always Online: connection closed, starting reconnect timer");
            SetDeviceState(kDeviceStateIdle);
            StartReconnectTimer();
#else
            SetDeviceState(kDeviceStateIdle);
#endif
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                // 开始预缓冲：收到足够音频数据后再播放，避免断断续续
                audio_service_.StartPrebuffering();
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                // 停止预缓冲，播放剩余数据
                audio_service_.StopPrebuffering();
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        // 设置等待播放完成标志，让音频播放完再切换状态
                        // MAIN_EVENT_PLAYBACK_IDLE 事件会在播放队列为空时触发状态切换
                        if (audio_service_.IsIdle()) {
                            // 如果播放队列已空，立即切换状态
#if CONFIG_ALWAYS_ONLINE
                            // Always Online 模式：始终保持监听状态
                            SetDeviceState(kDeviceStateListening);
#else
                            if (listening_mode_ == kListeningModeManualStop) {
                                SetDeviceState(kDeviceStateIdle);
                            } else {
                                SetDeviceState(kDeviceStateListening);
                            }
#endif
                        } else {
                            // 播放队列非空，等待播放完成
                            ESP_LOGI(TAG, "TTS stop received, waiting for playback to complete");
                            waiting_for_playback_complete_ = true;
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, message = std::string(text->valuestring)]() {
                        EventBridge::EmitSetText(message.c_str(), "assistant");
                    });
                }
                // Parse emotion from TTS
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(emotion)) {
                    ESP_LOGI(TAG, "Received emotion from TTS: %s", emotion->valuestring);
                    EventBridge::EmitSetEmotion(emotion->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, message = std::string(text->valuestring)]() {
                    EventBridge::EmitSetText(message.c_str(), "user");
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            // 处理 LLM 文本消息 (流式)
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text) && strlen(text->valuestring) > 0) {
                ESP_LOGI(TAG, "<< %s", text->valuestring);
                Schedule([this, message = std::string(text->valuestring)]() {
                    EventBridge::EmitSetText(message.c_str(), "assistant");
                });
            }
            // 处理情感 (可选) - 使用事件系统实现过渡动画
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                ESP_LOGI(TAG, "Received emotion from server: %s", emotion->valuestring);
                EventBridge::EmitSetEmotion(emotion->valuestring);
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::P3_SUCCESS);

#if CONFIG_ALWAYS_ONLINE
        // 启动后自动连接服务器，保持在线状态
        ESP_LOGI(TAG, "Always Online mode enabled, auto-connecting to server...");
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (protocol_->OpenAudioChannel()) {
                    // 连接成功后进入监听状态（但不发送音频，只保持心跳）
                    SetDeviceState(kDeviceStateListening);
                    ESP_LOGI(TAG, "Always Online: connected and listening");
                } else {
                    // 连接失败，启动重连定时器持续重试
                    ESP_LOGW(TAG, "Always Online: initial connection failed, starting reconnect timer");
                    SetDeviceState(kDeviceStateIdle);
                    StartReconnectTimer();
                }
            }
        });
#endif
    }
    // Print heap stats
    SystemInfo::PrintHeapStats();
}

void Application::OnClockTimer() {
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        //SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        //SystemInfo::PrintTaskList();
        //SystemInfo::PrintHeapStats();
        //SystemInfo::PrintPsramHeapStats();
        //SystemInfo::MonitorCpuUsage();
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    ESP_LOGI(TAG, "[Schedule] >> Acquiring mutex...");
    auto start = esp_timer_get_time();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto elapsed = (esp_timer_get_time() - start) / 1000;
        if (elapsed > 10) {
            ESP_LOGW(TAG, "[Schedule] Mutex acquired after %lld ms", elapsed);
        }
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
    ESP_LOGI(TAG, "[Schedule] << Task queued");
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_ERROR |
            MAIN_EVENT_PLAYBACK_IDLE, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            // 在 Speaking 状态时暂停发送 ASR 音频，减少 UART 竞争导致的卡顿
            if (device_state_ == kDeviceStateSpeaking) {
                // 清空队列，避免溢出（静默丢弃，不打印日志以减少 UART 竞争）
                int discarded = 0;
                while (audio_service_.PopPacketFromSendQueue()) { discarded++; }
                // 每 10 次才打印一次，减少日志量
                static int discard_log_counter = 0;
                if (discarded > 0 && ++discard_log_counter % 10 == 0) {
                    ESP_LOGD(TAG, "Speaking: discarded %d ASR packets", discarded);
                }
            } else {
                while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                    if (!protocol_->SendAudio(std::move(packet))) {
                        break;
                    }
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_PLAYBACK_IDLE) {
            // 播放队列已空，如果正在等待播放完成，则切换状态
            if (waiting_for_playback_complete_ && device_state_ == kDeviceStateSpeaking) {
                waiting_for_playback_complete_ = false;
                ESP_LOGI(TAG, "Playback complete, switching to listening mode");
#if CONFIG_ALWAYS_ONLINE
                // Always Online 模式：始终保持监听状态
                SetDeviceState(kDeviceStateListening);
#else
                if (listening_mode_ == kListeningModeManualStop) {
                    SetDeviceState(kDeviceStateIdle);
                } else {
                    SetDeviceState(kDeviceStateListening);
                }
#endif
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::P3_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            EventBridge::EmitSetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            EventBridge::EmitSetEmotion("thinking");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            EventBridge::EmitSetEmotion("neutral");

            // Always send listen message when entering listening state
            // (even if audio processor is already running, e.g., after reconnection)
            protocol_->SendStartListening(listening_mode_);

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
                audio_service_.EnableWakeWordDetection(true);
#else
                audio_service_.EnableWakeWordDetection(false);
#endif
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            PlaySound(Lang::Sounds::P3_AEC_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            PlaySound(Lang::Sounds::P3_AEC_NO);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            PlaySound(Lang::Sounds::P3_AEC_NO);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

#if CONFIG_ALWAYS_ONLINE
void Application::StartReconnectTimer() {
    if (reconnect_timer_ == nullptr) {
        return;
    }
    // 停止之前的定时器（如果正在运行）
    esp_timer_stop(reconnect_timer_);
    // 启动周期性重连定时器
    esp_timer_start_periodic(reconnect_timer_, RECONNECT_INTERVAL_MS * 1000);
    ESP_LOGI(TAG, "Always Online: reconnect timer started (interval: %dms)", RECONNECT_INTERVAL_MS);
}

void Application::StopReconnectTimer() {
    if (reconnect_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(reconnect_timer_);
    reconnect_retry_count_ = 0;
    ESP_LOGI(TAG, "Always Online: reconnect timer stopped");
}

void Application::OnReconnectTimer() {
    // 在定时器回调中调度到主循环执行，避免线程问题
    Schedule([this]() {
        // 如果已经连接，停止重连定时器
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            StopReconnectTimer();
            return;
        }

        reconnect_retry_count_++;
        ESP_LOGI(TAG, "Always Online: reconnect attempt #%d", reconnect_retry_count_);

        // 检查是否需要重置网络 (每 NETWORK_RESET_THRESHOLD 次失败后重置一次)
        if (reconnect_retry_count_ > 0 && reconnect_retry_count_ % NETWORK_RESET_THRESHOLD == 0) {
            ESP_LOGW(TAG, "Always Online: %d consecutive failures, attempting network reset...",
                     reconnect_retry_count_);

            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();

            // 显示提示
            Alert(Lang::Strings::ERROR, "正在重置网络...", "thinking", Lang::Sounds::P3_EXCLAMATION);
            display->SetStatus(Lang::Strings::REGISTERING_NETWORK);

            if (board.ResetNetwork()) {
                ESP_LOGI(TAG, "Always Online: network reset successful, retrying connection...");
            } else {
                ESP_LOGE(TAG, "Always Online: network reset failed, will continue retrying");
                Alert(Lang::Strings::ERROR, "网络重置失败", "sad", Lang::Sounds::P3_ERR_REG);
            }
        }

        SetDeviceState(kDeviceStateConnecting);
        if (protocol_->OpenAudioChannel()) {
            SetDeviceState(kDeviceStateListening);
            StopReconnectTimer();
            ESP_LOGI(TAG, "Always Online: reconnected successfully after %d attempts", reconnect_retry_count_);
        } else {
            SetDeviceState(kDeviceStateIdle);
            ESP_LOGW(TAG, "Always Online: reconnect attempt #%d failed, will retry in %dms",
                     reconnect_retry_count_, RECONNECT_INTERVAL_MS);
            // 定时器会继续触发下一次重试
        }
    });
}
#endif