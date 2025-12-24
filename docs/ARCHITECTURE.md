# 小智 AI 聊天机器人（zhengchen-eye）架构设计文档

## 一、项目概述

| 属性 | 值 |
|------|-----|
| **项目名称** | 小智 AI 聊天机器人（xiaozhi-esp32） |
| **板型变种** | zhengchen-eye |
| **项目类型** | ESP32 嵌入式 AI 助手固件 |
| **版本** | 1.8.5 |
| **许可证** | MIT |
| **技术栈** | C++17、ESP-IDF 5.4+、FreeRTOS |

项目是一个基于 ESP32 微控制器的语音交互 AI 助手，支持 70+ 种开源硬件平台，可通过 MCP 协议进行物联网设备控制。

---

## 二、技术栈

### 2.1 核心依赖

| 组件 | 版本 | 用途 |
|------|------|------|
| LVGL | 9.2.2 | GUI 图形库 |
| esp_lvgl_port | 2.6.0 | LVGL 显示适配 |
| esp-opus-encoder | 2.4.1 | 音频编解码 |
| esp-sr | 2.1.4 | 离线语音识别/唤醒词 |
| esp-wifi-connect | 2.4.3 | WiFi 连接管理 |
| esp-ml307 | 3.2.6 | ML307 Cat.1 4G 模块 |
| led_strip | 2.5.5 | LED 控制 |
| esp-codec-dev | 1.3.6 | 音频编解码器驱动 |
| button | 4.1.3 | 按钮输入 |

### 2.2 显示驱动支持

- **LCD**: ILI9341, GC9A01, ST77916, AXS15231B, SH8601, SPD2010, NV3023, GC9D01N
- **触摸屏**: FT5X06, GT911, CST9217, CST816S（电容屏）
- **OLED**: SH1106

### 2.3 编程规范

- **语言标准**: C++17
- **代码风格**: Google C++ Style Guide
- **编译优化**: 优化为大小（CONFIG_COMPILER_OPTIMIZATION_SIZE）
- **异常处理**: 启用 C++ 异常支持

---

## 三、系统架构

### 3.1 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层（Application）                      │
│         - 设备状态管理 - 事件调度 - 业务逻辑                   │
└────────────┬─────────────────────────────────┬──────────────┘
             │                                 │
      ┌──────▼───────┐                 ┌───────▼───────┐
      │  音频服务     │                 │   显示模块     │
      │ AudioService │                 │   Display     │
      │              │                 │  (LCD/OLED)   │
      └──────┬───────┘                 └───────┬───────┘
             │                                 │
      ┌──────▼───────┐                 ┌───────▼───────┐
      │  编解码链     │                 │   LED 控制    │
      │ - OPUS编码   │                 │ - 单LED       │
      │ - 音频处理   │                 │ - LED灯带     │
      │ - 唤醒词检测 │                 │ - GPIO LED    │
      └──────┬───────┘                 └───────┬───────┘
             │                                 │
             └────────────┬────────────────────┘
                          │
              ┌───────────▼───────────┐
              │     通信协议层         │
              │   - WebSocket         │
              │   - MQTT + UDP        │
              │   - MCP 服务器        │
              └───────────┬───────────┘
                          │
              ┌───────────▼───────────┐
              │    网络/硬件抽象       │
              │    Board Interface    │
              │   - 70+ 开发板支持    │
              └───────────────────────┘
```

### 3.2 模块依赖图

```
应用程序 (main.cc)
    │
    ▼
Application (状态机与事件循环)
    ├──► Board (硬件抽象)
    │    ├──► AudioCodec (音频编解码芯片)
    │    ├──► Display (显示屏)
    │    ├──► Led (LED 控制)
    │    ├──► NetworkInterface (网络)
    │    └──► Camera (摄像头)
    │
    ├──► AudioService (音频处理核心)
    │    ├──► AudioCodec
    │    ├──► AudioProcessor (VAD/AEC)
    │    ├──► WakeWord (唤醒词检测)
    │    └──► OPUS 编解码库
    │
    ├──► Protocol (通信协议)
    │    ├──► WebSocketProtocol
    │    └──► MqttProtocol
    │
    ├──► McpServer (物联网控制)
    │    └──► JSON-RPC 处理
    │
    ├──► Ota (固件升级)
    │    └──► Http 客户端
    │
    └──► Display (UI 显示)
         ├──► LVGL 9.2.2
         ├──► LCD/OLED 驱动
         ├──► 触摸屏驱动
         └──► 自定义字体/表情
```

---

## 四、核心模块详解

### 4.1 Application 类（应用主控制器）

**文件位置**: `main/application.{h,cc}` (771 行)

**设计模式**: 单例模式

**主要职责**:
- 应用启动与事件循环主控
- 设备状态管理（11 种状态）
- 任务调度队列（std::deque 支持）
- 音频服务与协议协调
- OTA 更新检查

**核心接口**:

```cpp
class Application {
public:
    static Application& GetInstance();

    void Start();                           // 启动应用
    void MainEventLoop();                   // 主事件循环
    DeviceState GetDeviceState();           // 获取设备状态
    void Schedule(std::function<void()>);   // 任务调度
    void SendMcpMessage(std::string);       // MCP 消息发送
    void SetAecMode(AecMode);               // 回声消除模式
};
```

**设备状态机（11 种状态）**:

| 状态 | 说明 |
|------|------|
| kDeviceStateUnknown | 未知 |
| kDeviceStateStarting | 启动中 |
| kDeviceStateWifiConfiguring | WiFi 配置 |
| kDeviceStateIdle | 空闲 |
| kDeviceStateConnecting | 连接中 |
| kDeviceStateListening | 监听中 |
| kDeviceStateSpeaking | 说话中 |
| kDeviceStateUpgrading | 升级中 |
| kDeviceStateActivating | 激活中 |
| kDeviceStateAudioTesting | 音频测试 |
| kDeviceStateFatalError | 致命错误 |

---

### 4.2 AudioService 模块

**文件位置**: `main/audio/audio_service.{h,cc}` (585 行)

**音频处理流程**:

```
输入流：
MIC ──► [Audio Processors] ──► {Encode Queue} ──► [OPUS Encoder]
    ──► {Send Queue} ──► Server

输出流：
Server ──► {Decode Queue} ──► [OPUS Decoder] ──► {Playback Queue} ──► Speaker
```

**关键参数**:

| 参数 | 值 | 说明 |
|------|-----|------|
| OPUS_FRAME_DURATION_MS | 60 | OPUS 帧时长 |
| MAX_ENCODE_TASKS_IN_QUEUE | 2 | 编码任务队列容量 |
| AUDIO_POWER_TIMEOUT_MS | 15000 | 音频电源超时 |

**任务结构**:
- `AudioInputTask()`: 麦克风输入处理
- `AudioOutputTask()`: 扬声器输出处理
- `OpusCodecTask()`: OPUS 编解码处理

**功能配置**:
- 唤醒词检测（AFE 或 ESP-SR）
- 音频处理器（降噪、VAD）
- 自定义唤醒词
- 音频测试模式
- 设备端/服务器端 AEC（回声消除）

---

### 4.3 Display 显示模块

**文件位置**: `main/display/`

**代码量**: lcd_display.cc (1175 行) + oled_display.cc (309 行) + display.cc (266 行)

**类层次结构**:

```
Display（基类）
├── LcdDisplay（LCD 屏实现）
├── OledDisplay（OLED 屏实现）
├── NoDisplay（无屏实现）
└── EsplogDisplay（ESP 日志显示）
```

**核心功能**:
- 显示状态、通知、聊天消息
- 表情显示（9 种情绪表情 GIF）
- 状态栏更新（网络、电池、静音图标）
- 主题管理
- 省电模式（降低刷新率）
- LVGL 图形库集成

**支持的显示分辨率**:
- 240x240（GIF1/GIF2 两种表情集）
- 160x160（GIF1/GIF2 两种表情集）

---

### 4.4 Protocol 协议层

**文件位置**: `main/protocols/`

**代码量**: mqtt_protocol.cc (335 行) + websocket_protocol.cc (253 行)

#### WebSocket 协议

- JSON-RPC 2.0 格式
- 实时双向通信
- 支持音频流与 JSON 消息混合

#### MQTT + UDP 混合协议

- MQTT 用于 JSON 控制消息
- UDP 用于低延迟音频流
- 适合网络不稳定场景

**Protocol 基类接口**:

```cpp
class Protocol {
public:
    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
    virtual bool SendAudio(AudioStreamPacket) = 0;
    virtual bool SendText(std::string) = 0;

    // 回调注册
    void OnIncomingAudio(std::function<void(AudioPacket)>);
    void OnIncomingJson(std::function<void(cJSON*)>);
    void OnAudioChannelOpened(std::function<void()>);
    void OnNetworkError(std::function<void(std::string)>);
};
```

**二进制协议格式 (Protocol v3)**:

```cpp
struct BinaryProtocol3 {
    uint8_t type;           // 消息类型
    uint8_t reserved;       // 保留字段
    uint16_t payload_size;  // 负载大小
    uint8_t payload[];      // 负载数据
};
```

---

### 4.5 MCP Server 模块

**文件位置**: `main/mcp_server.{h,cc}` (395 行)

**协议**: Model Context Protocol（JSON-RPC 2.0）

**工具注册系统**:

```cpp
class McpTool {
    std::string name;        // 工具名称: self.device.action 格式
    std::string description; // 自然语言说明
    PropertyList properties; // 属性列表
    Callback callback;       // 回调函数
};
```

**属性类型支持**:

```cpp
enum PropertyType {
    kPropertyTypeBoolean,      // bool
    kPropertyTypeInteger,      // int（支持范围限制）
    kPropertyTypeString        // string
};
```

**工具调用流程**:

1. 后台请求 `tools/list` 获取设备支持的所有工具
2. 后台通过 `tools/call` 调用具体工具
3. 设备执行回调并返回结果
4. 结果以 JSON 格式返回

---

### 4.6 LED 模块

**文件位置**: `main/led/`

| 类 | 代码量 | 用途 |
|----|--------|------|
| SingleLed | 195 行 | 单个 LED（GPIO 控制） |
| CircularStrip | 233 行 | WS2812B/SK6812 RGB 灯带 |
| GpioLed | 249 行 | GPIO LED 组 |

**功能**:
- 颜色控制（RGB）
- 亮度调节
- 闪烁/呼吸效果
- 动画支持

---

### 4.7 Board 抽象层

**文件位置**: `main/boards/`

**支持板型数量**: 70+

**Board 接口（工厂模式）**:

```cpp
class Board {
public:
    static Board& GetInstance();  // 工厂方法

    virtual AudioCodec* GetAudioCodec() = 0;
    virtual Display* GetDisplay() = 0;
    virtual Led* GetLed() = 0;
    virtual NetworkInterface* GetNetwork() = 0;
    virtual Camera* GetCamera() = 0;
    virtual bool GetBatteryLevel(int& level, bool& charging) = 0;
    virtual std::string GetBoardJson() = 0;
};
```

---

## 五、zhengchen_eye 板型配置

**配置文件位置**: `main/boards/zhengchen_eye/`

### GPIO 引脚分配

| 功能 | GPIO |
|------|------|
| I2S MCLK | 38 |
| I2S WS | 13 |
| I2S BCLK | 14 |
| I2S DIN | 12 |
| I2S DOUT | 45 |
| 显示屏 SDA | 43 |
| 显示屏 SCL | 44 |
| 显示屏 RES | 46 |
| 显示屏 DC | 8 |
| 背光 | 42 |
| BOOT 按钮 | 0 |
| LED | 3 |
| 触摸键 1 | 4 |
| 触摸键 2 | 5 |
| ML307 RX | 48 |
| ML307 TX | 47 |

### 硬件配置

| 组件 | 型号/规格 |
|------|----------|
| 音频编解码器 | ES8311 |
| 显示屏 | GC9D01N 圆形 LCD |
| 分辨率 | 160x160 或 240x240 |
| 网络 | WiFi + ML307 4G 双网络 |

---

## 六、通信流程

### 6.1 语音交互流程

```
┌──────┐              ┌────────┐              ┌──────┐
│ 设备 │              │ 服务器 │              │ 模型 │
└──┬───┘              └───┬────┘              └──┬───┘
   │                      │                      │
   │ 1. 唤醒词检测         │                      │
   ├─────────────────────►│                      │
   │ 2. SendWakeWordDetected                     │
   │                      │                      │
   │ 3. OpenAudioChannel  │                      │
   ├─────────────────────►│                      │
   │                      │                      │
   │ 4. 发送音频数据       │                      │
   │ (OPUS 编码)          │ 5. 发送到模型        │
   ├─────────────────────►├─────────────────────►│
   │                      │                      │
   │                      │ 6. ASR + LLM 处理    │
   │                      │◄─────────────────────┤
   │                      │                      │
   │ 7. 接收 TTS 音频      │                      │
   │◄─────────────────────┤                      │
   │                      │                      │
   │ 8. OPUS 解码         │                      │
   │ 9. 播放扬声器        │                      │
   │                      │                      │
```

### 6.2 MCP 工具调用流程

```
大模型 ──► 分析意图 ──► "调用 self.light.set_rgb"
                           │
                           ▼
              发送 JSON-RPC 请求到设备
                           │
                           ▼
              设备收到请求 ──► 解析工具名 & 参数
                           │
                           ▼
              执行注册回调 ──► 返回结果
                           │
                           ▼
              结果返回到模型 ──► 模型反馈给用户
```

---

## 七、线程模型

### FreeRTOS 任务分配

```
主线程（APP_CPU）
├── AudioService 任务
│   ├── AudioInputTask（麦克风采集）
│   ├── AudioOutputTask（扬声器播放）
│   └── OpusCodecTask（编解码处理）
├── 协议通信任务
│   ├── WebSocket 接收任务
│   └── MQTT 接收任务
├── Display 刷新任务
├── 按钮处理任务
└── OTA 检查任务
```

### 同步机制

| 机制 | 用途 |
|------|------|
| 互斥锁 (Mutex) | 保护共享资源 |
| 条件变量 (Condition) | 音频队列同步 |
| 事件组 (EventGroup) | 任务间事件通知 |
| 消息队列 (Queue) | 任务间数据传递 |

---

## 八、设计模式应用

| 模式 | 应用位置 | 用途 |
|------|---------|------|
| 单例模式 | Application, Board, McpServer | 全局资源管理 |
| 工厂模式 | Board::GetInstance() | 硬件抽象创建 |
| 策略模式 | Protocol（WebSocket/MQTT） | 协议灵活切换 |
| 观察者模式 | 事件回调系统 | 音频/网络事件通知 |
| 模板方法 | Display 基类 | 显示逻辑统一 |
| 适配器模式 | AudioCodec 各实现 | 不同音频芯片适配 |

---

## 九、配置系统

### 9.1 Kconfig 主要配置项

**板型选择 (BOARD_TYPE)**:
- 面包板方案（WiFi/ML307/ESP32/LCD）
- 虾哥系列（Mini C3 v3/4G）
- M5Stack（CoreS3/Tab5）
- 微雪、立创、DFRobot 等 70+ 种

**语言选择 (LANGUAGE)**:
- 中文（简体/繁体）
- 英文（美国）
- 日文

**音频处理 (AUDIO)**:
- 唤醒词检测方式（AFE/ESP-SR/Custom）
- 音频处理器启用
- AEC 模式（设备端/服务器端）

**显示屏 (LCD/OLED)**:
- 分辨率选择（160x160/240x240）
- 表情集选择（GIF1/GIF2）
- 屏幕类型（LCD/OLED/无屏）

### 9.2 sdkconfig 关键配置

```ini
CONFIG_COMPILER_OPTIMIZATION_SIZE=y           # 优化为大小
CONFIG_COMPILER_CXX_EXCEPTIONS=y              # C++ 异常支持
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192          # 主任务栈
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y               # 动态 SSL 缓冲
CONFIG_LV_FONT_FMT_TXT_LARGE=y                # 大字体格式
CONFIG_LV_USE_FONT_COMPRESSED=y               # 字体压缩
```

---

## 十、文件结构统计

| 模块 | 代码量 | 占比 |
|------|--------|------|
| 显示模块 | 1747 行 | 30.4% |
| 应用层 | 771 行 | 13.4% |
| LED 控制 | 677 行 | 11.8% |
| 通信协议 | 588 行 | 10.2% |
| 音频服务 | 585 行 | 10.2% |
| OTA 更新 | 447 行 | 7.8% |
| MCP 服务器 | 395 行 | 6.9% |
| 系统模块 | 301 行 | 5.2% |
| 其他 | 237 行 | 4.1% |
| **总计** | **5748 行** | **100%** |

---

## 十一、构建与部署

### 11.1 编译命令

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置板型和语言
idf.py menuconfig

# 编译
idf.py build

# 烧录并监控
idf.py flash monitor
```

### 11.2 分区表 (16MB Flash)

| 偏移量 | 大小 | 用途 |
|--------|------|------|
| 0x0 | 32KB | 引导程序 |
| 0x8000 | 8KB | 分区表 |
| 0x10000 | 11MB | 应用程序 (OTA A) |
| 0xB10000 | 4MB | OTA B |
| 0xF10000 | 1MB | 文件系统/数据 |

---

## 十二、扩展开发

### 12.1 新增 MCP 工具

```cpp
auto& mcp = McpServer::GetInstance();
mcp.AddTool(
    "self.device.custom",           // 工具名称
    "自定义功能描述",                // 描述
    PropertyList({                   // 参数列表
        Property("param", kPropertyTypeInteger, 0, 100)
    }),
    [](const PropertyList& props) -> ReturnValue {
        // 实现逻辑
        int value = props.GetInteger("param");
        return true;
    }
);
```

### 12.2 新增音频编解码器

1. 继承 `AudioCodec` 基类
2. 实现初始化和编解码方法
3. 在 CMakeLists.txt 中注册

### 12.3 新增显示类型

1. 继承 `Display` 基类
2. 实现 LVGL 集成
3. 支持刷新和事件处理

### 12.4 自定义板型

参考 `main/boards/README.md`：
1. 创建新的 `boards/custom-board/` 目录
2. 实现 Board 接口
3. 配置 GPIO 和外设

---

## 十三、项目特色

1. **硬件适配性强**: 支持 70+ 个开源硬件平台，统一的 Board 接口
2. **云端集成**: MCP 协议支持云端工具调用，流式音频处理架构
3. **离线能力**: 本地唤醒词检测，离线音频处理
4. **开发友好**: 清晰的模块划分，丰富的代码注释，Google C++ 代码规范
5. **资源优化**: 适配小内存 ESP32-C3，字体和表情压缩，PSRAM 利用

---

## 十四、参考资料

- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/)
- [LVGL 文档](https://docs.lvgl.io/)
- [ESP-SR 文档](https://github.com/espressif/esp-sr)
- [MCP 协议规范](https://modelcontextprotocol.io/)

---

**文档版本**: 1.0
**最后更新**: 2025-12-18
**维护者**: Claude Code
