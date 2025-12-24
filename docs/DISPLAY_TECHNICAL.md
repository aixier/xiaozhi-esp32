# zhengchen-eye 显示系统技术文档

## 1. 系统架构

### 1.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                    Application Layer                             │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Application (application.cc)                            │    │
│  │  - 状态机控制 (Idle/Connecting/Listening/Speaking)       │    │
│  │  - SetEmotion() / SetStatus() / SetChatMessage()        │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Display Abstraction Layer                     │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Display (display.h / display.cc)                        │    │
│  │  - 抽象基类，定义显示接口                                  │    │
│  │  - SetStatus() / SetEmotion() / SetChatMessage()        │    │
│  │  - UpdateStatusBar() / ShowNotification()               │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                   │
│                              ▼                                   │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  LcdDisplay (lcd_display.h / lcd_display.cc)             │    │
│  │  - LCD 显示实现                                          │    │
│  │  - GIF 动画支持 (lv_gif)                                 │    │
│  │  - 主题系统 (深色/浅色)                                   │    │
│  │  - UI 布局管理                                           │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                   │
│  ┌────────────┬──────────────┼──────────────┬────────────┐      │
│  ▼            ▼              ▼              ▼            ▼      │
│ SpiLcd    RgbLcd         MipiLcd       QspiLcd     Mcu8080Lcd   │
│ Display   Display        Display       Display      Display     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       LVGL Layer                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  esp_lvgl_port (v2.6.0)                                  │    │
│  │  - LVGL 任务管理                                         │    │
│  │  - 显示缓冲区管理                                        │    │
│  │  - 触摸输入处理                                          │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  LVGL v9.2.2                                             │    │
│  │  - lv_gif: GIF 动画组件                                  │    │
│  │  - lv_label: 文本标签                                    │    │
│  │  - lv_obj: 容器/布局                                     │    │
│  │  - lv_img: 图像显示                                      │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      ESP-IDF LCD Driver                          │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  esp_lcd_gc9a01 (v2.0.1)                                 │    │
│  │  - GC9A01 LCD 控制器驱动                                 │    │
│  │  - 240x240 圆形 TFT 显示屏                               │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  esp_lcd_panel_io_spi                                    │    │
│  │  - SPI 面板 IO 驱动                                      │    │
│  │  - DMA 传输支持                                          │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Hardware Layer                              │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  SPI Bus (SPI2_HOST)                                     │    │
│  │  - MOSI: GPIO43                                          │    │
│  │  - SCLK: GPIO44                                          │    │
│  │  - Clock: 40MHz                                          │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  GC9A01 LCD Panel (240x240)                              │    │
│  │  - DC: GPIO8                                             │    │
│  │  - RST: GPIO46                                           │    │
│  │  - Backlight: GPIO42 (PWM)                               │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 类继承关系

```
Display (抽象基类)
    │
    ├── NoDisplay (空实现)
    │
    ├── OledDisplay (OLED 显示)
    │
    └── LcdDisplay (LCD 显示)
            │
            ├── SpiLcdDisplay    ← zhengchen-eye 使用
            ├── RgbLcdDisplay
            ├── MipiLcdDisplay
            ├── QspiLcdDisplay
            └── Mcu8080LcdDisplay
```

---

## 2. 硬件配置

### 2.1 引脚定义 (config.h)

| 功能 | GPIO | 说明 |
|------|------|------|
| DISPLAY_SDA (MOSI) | GPIO43 | SPI 数据线 |
| DISPLAY_SCL (SCLK) | GPIO44 | SPI 时钟线 |
| DISPLAY_DC | GPIO8 | 数据/命令选择 |
| DISPLAY_RES | GPIO46 | 复位引脚 |
| DISPLAY_CS | NC | 片选 (未使用) |
| DISPLAY_BACKLIGHT | GPIO42 | 背光 PWM 控制 |

### 2.2 显示参数

| 参数 | 值 | 说明 |
|------|------|------|
| 分辨率 | 240x240 | 正方形显示区域 |
| 颜色格式 | RGB565 | 16位色深 |
| SPI 时钟 | 40MHz | 高速传输 |
| 刷新缓冲 | 240x20 | 分块传输 |
| SWAP_XY | true | XY 坐标交换 |
| MIRROR_X | true | X 轴镜像 |
| MIRROR_Y | false | Y 轴不镜像 |

### 2.3 LCD 驱动芯片

**GC9A01** (正确配置):
- 240x240 分辨率
- 圆形 TFT 显示屏
- SPI 接口

**GC9D01N** (错误配置):
- 160x160 分辨率
- 会导致显示不正常

---

## 3. 软件模块详解

### 3.1 Display 基类 (display.h)

```cpp
class Display {
public:
    // 状态显示
    virtual void SetStatus(const char* status);

    // 表情/动画
    virtual void SetEmotion(const char* emotion);

    // 聊天消息
    virtual void SetChatMessage(const char* role, const char* content);

    // 图标显示
    virtual void SetIcon(const char* icon);

    // 主题切换
    virtual void SetTheme(const std::string& theme_name);

    // 状态栏更新
    virtual void UpdateStatusBar(bool update_all = false);

    // 省电模式
    virtual void SetPowerSaveMode(bool on);

protected:
    int width_, height_;
    lv_display_t* display_;

    // UI 组件
    lv_obj_t* emotion_label_;     // 表情图标
    lv_obj_t* gif_label_;         // GIF 动画
    lv_obj_t* status_label_;      // 状态文字
    lv_obj_t* battery_label_;     // 电量图标
    lv_obj_t* network_label_;     // 网络图标
    lv_obj_t* chat_message_label_;// 聊天消息
};
```

### 3.2 LcdDisplay 类 (lcd_display.cc)

#### 3.2.1 初始化流程

```cpp
SpiLcdDisplay::SpiLcdDisplay(...) {
    // 1. 初始化 LVGL 端口
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;  // 50ms 刷新周期
    lvgl_port_init(&port_cfg);

    // 2. 配置显示参数
    lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = width_ * 20,  // 分块缓冲
        .double_buffer = true,
        .hres = width_,
        .vres = height_,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
    };

    // 3. 添加显示设备
    display_ = lvgl_port_add_disp(&display_cfg);

    // 4. 设置 UI
    SetupUI();
}
```

#### 3.2.2 UI 布局 (GIF 风格)

```cpp
void LcdDisplay::SetupUI() {
    // 获取活动屏幕
    lv_obj_t* screen = lv_scr_act();

    // 主容器 (全屏)
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);

    // 状态栏 (顶部)
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);

    // GIF 叠加层 (全屏覆盖)
    overlay_container = lv_obj_create(screen);
    lv_obj_set_size(overlay_container, LV_HOR_RES, LV_VER_RES);

    // GIF 标签
    gif_label_ = lv_gif_create(overlay_container);
    lv_gif_set_src(gif_label_, &happy);  // 默认 happy 表情
    lv_obj_set_size(gif_label_, LV_HOR_RES, LV_VER_RES);

    // 内容区域 (聊天消息)
    content_ = lv_obj_create(container_);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);

    // 聊天消息标签
    chat_message_label_ = lv_label_create(content_);
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
}
```

### 3.3 表情系统 (SetEmotion)

#### 3.3.1 表情映射表

```cpp
static const std::vector<Emotion> emotions = {
    // GIF 指针        情感名称
    {&happy,     "neutral"},
    {&happy,     "happy"},
    {&happy,     "laughing"},
    {&happy,     "funny"},
    {&sad,       "sad"},
    {&angry,     "angry"},
    {&sad,       "crying"},
    {&love,      "loving"},
    {&confused,  "embarrassed"},
    {&delicious, "surprised"},
    {&delicious, "shocked"},
    {&thinking,  "thinking"},
    {&cool,      "winking"},
    {&cool,      "cool"},
    {&happy,     "relaxed"},
    {&delicious, "delicious"},
    {&love,      "kissy"},
    {&confused,  "confident"},
    {&sleepy,    "sleepy"},
    {&delicious, "silly"},
    {&confused,  "confused"}
};
```

#### 3.3.2 SetEmotion 实现

```cpp
void LcdDisplay::SetEmotion(const char* emotion) {
    // 查找匹配的表情
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [emotion](const Emotion& e) {
            return strcmp(e.text, emotion) == 0;
        });

    DisplayLockGuard lock(this);

    if (it != emotions.end()) {
        // 设置 GIF 源
        lv_gif_set_src(gif_label_, it->gif);
    } else {
        // 未知表情，使用默认
        lv_gif_set_src(gif_label_, &happy);
    }
}
```

---

## 4. GIF 动画资源

### 4.1 资源文件

位置: `/main/assets/gif1_240/`

| 文件 | 大小 | 表情 |
|------|------|------|
| angry.c | 297KB | 生气 |
| confused.c | 174KB | 困惑 |
| cool.c | 114KB | 酷/眨眼 |
| delicious.c | 133KB | 美味/惊讶 |
| happy.c | 260KB | 开心 (默认) |
| love.c | 299KB | 爱心 |
| sad.c | 215KB | 悲伤 |
| sleepy.c | 207KB | 困倦 |
| thinking.c | 417KB | 思考 |

### 4.2 GIF 数据结构

```cpp
// GIF 图像描述符 (LVGL 格式)
LV_IMG_DECLARE(happy);

// 编译后的 GIF 数据 (angry.c 示例)
const lv_img_dsc_t angry = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RAW,
        .w = 240,
        .h = 240,
    },
    .data_size = 297410,
    .data = angry_map,  // 原始 GIF 数据
};
```

### 4.3 配置选项

```c
// sdkconfig 配置
CONFIG_USE_LCD_240X240_GIF1=y    // 240x240 GIF 风格 1 (正确)
# CONFIG_USE_LCD_160X160_GIF1    // 160x160 GIF 风格 1 (错误)
# CONFIG_USE_LCD_240X240_GIF2    // 240x240 GIF 风格 2
# CONFIG_USE_LCD_160X160_GIF2    // 160x160 GIF 风格 2
```

---

## 5. 初始化时序

### 5.1 启动时序图

```
Board Init
    │
    ├─[1] InitializePowerManager()
    │     └── 电源管理初始化
    │
    ├─[2] InitializePowerSaveTimer()
    │     └── 省电定时器配置
    │
    ├─[3] InitializeCodecI2c()
    │     └── I2C 总线初始化
    │
    ├─[4] InitializeButtons()
    │     └── 按钮配置
    │
    ├─[5] InitializeSpi()
    │     │
    │     ├── spi_bus_initialize(SPI2_HOST, ...)
    │     │   └── MOSI=43, SCLK=44, 40MHz
    │     │
    │     └── SPI 总线就绪
    │
    ├─[6] InitializeGc9107Display()
    │     │
    │     ├── esp_lcd_new_panel_io_spi()
    │     │   └── 创建 SPI 面板 IO
    │     │
    │     ├── esp_lcd_new_panel_gc9a01()
    │     │   └── 创建 GC9A01 面板驱动
    │     │
    │     ├── esp_lcd_panel_reset()
    │     │   └── 硬件复位 (RST=GPIO46)
    │     │
    │     ├── esp_lcd_panel_init()
    │     │   └── 发送初始化命令序列
    │     │
    │     ├── esp_lcd_panel_invert_color()
    │     │   └── 颜色反转配置
    │     │
    │     ├── esp_lcd_panel_swap_xy()
    │     │   └── XY 坐标交换
    │     │
    │     ├── esp_lcd_panel_mirror()
    │     │   └── 镜像配置
    │     │
    │     └── new SpiLcdDisplay()
    │         │
    │         ├── lvgl_port_init()
    │         │   └── LVGL 端口初始化
    │         │
    │         ├── lvgl_port_add_disp()
    │         │   └── 添加显示设备
    │         │
    │         ├── esp_lcd_panel_disp_on_off(true)
    │         │   └── 开启显示
    │         │
    │         └── SetupUI()
    │             │
    │             ├── 创建 container_
    │             ├── 创建 status_bar_
    │             ├── 创建 overlay_container
    │             ├── lv_gif_create() → gif_label_
    │             ├── lv_gif_set_src(&happy)
    │             ├── 创建 content_
    │             └── 创建 chat_message_label_
    │
    ├─[6.1] SetBacklight(100)
    │       └── PWM 背光设置
    │
    └─[7] InitializeTouch()
          └── 触摸初始化

Application Init
    │
    ├── SetDeviceState(kDeviceStateIdle)
    │   │
    │   ├── display->SetStatus("待机中")
    │   └── display->SetEmotion("neutral")
    │
    └── 进入主循环
```

### 5.2 显示更新时序

```
User Event (唤醒词/按钮)
    │
    ├── Application::SetDeviceState(kDeviceStateListening)
    │   │
    │   ├── display->SetStatus("聆听中...")
    │   └── display->SetEmotion("neutral")
    │
    ├── [语音识别完成]
    │   └── display->SetChatMessage("user", "用户说的话")
    │
    ├── [收到服务器响应]
    │   │
    │   ├── display->SetEmotion("happy")  // 根据服务器返回
    │   └── display->SetChatMessage("assistant", "AI回复")
    │
    └── [对话结束]
        └── Application::SetDeviceState(kDeviceStateIdle)
            │
            ├── display->SetStatus("待机中")
            └── display->SetEmotion("neutral")
```

---

## 6. 主题系统

### 6.1 颜色定义

```cpp
// 深色主题
const ThemeColors DARK_THEME = {
    .background       = 0x121212,  // 深色背景
    .text            = 0xFFFFFF,  // 白色文字
    .chat_background = 0x1E1E1E,  // 聊天区背景
    .user_bubble     = 0x1A6C37,  // 用户气泡 (深绿)
    .assistant_bubble= 0x333333,  // AI气泡 (深灰)
    .system_bubble   = 0x2A2A2A,  // 系统气泡
    .system_text     = 0xAAAAAA,  // 系统文字
    .border          = 0x333333,  // 边框
    .low_battery     = 0xFF0000,  // 低电量 (红色)
};

// 浅色主题
const ThemeColors LIGHT_THEME = {
    .background       = 0xFFFFFF,  // 白色背景
    .text            = 0x000000,  // 黑色文字
    .chat_background = 0xE0E0E0,  // 聊天区背景
    .user_bubble     = 0x95EC69,  // 用户气泡 (微信绿)
    .assistant_bubble= 0xFFFFFF,  // AI气泡 (白色)
    .system_bubble   = 0xE0E0E0,  // 系统气泡
    .system_text     = 0x666666,  // 系统文字
    .border          = 0xE0E0E0,  // 边框
    .low_battery     = 0x000000,  // 低电量 (黑色)
};
```

### 6.2 主题切换

```cpp
void LcdDisplay::SetTheme(const std::string& theme_name) {
    DisplayLockGuard lock(this);

    if (theme_name == "dark") {
        current_theme_ = DARK_THEME;
    } else {
        current_theme_ = LIGHT_THEME;
    }

    // 更新所有 UI 组件颜色
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    // ... 更新其他组件
}
```

---

## 7. 性能优化

### 7.1 双缓冲机制

```cpp
const lvgl_port_display_cfg_t display_cfg = {
    .buffer_size = width_ * 20,   // 每次传输 20 行
    .double_buffer = true,        // 启用双缓冲
    // ...
};
```

### 7.2 DMA 传输

```cpp
spi_bus_config_t bus_config = {
    .mosi_io_num = DISPLAY_SDA,
    .sclk_io_num = DISPLAY_SCL,
    .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
    // DMA 自动启用
};
```

### 7.3 LVGL 任务配置

```cpp
lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
port_cfg.task_priority = 1;       // 低优先级
port_cfg.timer_period_ms = 50;    // 50ms 刷新周期 (20 FPS)
```

---

## 8. 调试信息

### 8.1 启动日志示例

```
I (329) zhengchen_eye: === LCD Display Init Start ===
I (339) zhengchen_eye: Creating LCD panel IO, DC pin=8, PCLK=40MHz
I (349) zhengchen_eye: LCD panel IO create OK, handle=0x3c3c1248
I (359) zhengchen_eye: Installing GC9A01/GC9107 LCD driver, RST pin=46
I (369) gc9a01: LCD panel create success, version: 2.0.1
I (379) zhengchen_eye: GC9A01 panel create OK, handle=0x3fcc63a0
I (509) zhengchen_eye: GC9A01/GC9107 LCD driver init complete
I (509) zhengchen_eye: Creating SpiLcdDisplay: 240x240, offset=(0,0)
I (649) LcdDisplay: === SetupUI Start (GIF Style) ===
I (719) LcdDisplay: GIF label created, gif_label_=0x3fccb5d8
I (779) LcdDisplay: GIF initialized, size=240x240
I (779) LcdDisplay: === SetupUI Complete (GIF Style) ===
```

### 8.2 关键验证点

1. **LCD 驱动**: 必须显示 `gc9a01` 而非 `gc9d01n`
2. **分辨率**: 必须是 `240x240`
3. **GIF 初始化**: `GIF initialized, size=240x240`
4. **SetupUI 完成**: `SetupUI Complete (GIF Style)`

---

## 9. 故障排除

### 9.1 屏幕不显示

| 症状 | 可能原因 | 解决方案 |
|------|---------|---------|
| 全黑 | LCD 驱动错误 | 检查 `CONFIG_USE_LCD_240X240_GIF1=y` |
| 花屏 | SPI 配置错误 | 检查引脚定义和时钟频率 |
| 颜色反转 | 颜色配置错误 | 检查 `esp_lcd_panel_invert_color()` |
| 方向错误 | 镜像配置错误 | 检查 SWAP_XY, MIRROR_X, MIRROR_Y |

### 9.2 GIF 不播放

| 症状 | 可能原因 | 解决方案 |
|------|---------|---------|
| 静止不动 | LVGL 任务未运行 | 检查 `lvgl_port_init()` |
| 显示乱码 | GIF 资源错误 | 确认使用正确的资源目录 |
| 崩溃 | 内存不足 | 检查 PSRAM 配置 |

---

## 10. 相关文件

| 文件 | 说明 |
|------|------|
| `main/display/display.h` | Display 基类定义 |
| `main/display/display.cc` | Display 基类实现 |
| `main/display/lcd_display.h` | LcdDisplay 类定义 |
| `main/display/lcd_display.cc` | LcdDisplay 类实现 |
| `main/boards/zhengchen_eye/config.h` | 硬件引脚配置 |
| `main/boards/zhengchen_eye/zhengchen_eye.cc` | 板级初始化 |
| `main/assets/gif1_240/*.c` | GIF 动画资源 |
| `main/application.cc` | 应用层状态机 |

---

**文档版本**: 1.0
**最后更新**: 2025-12-19
**适用项目**: zhengchen-eye (ESP32-S3 + GC9A01 LCD)
