# 小智 AI 表情系统技术文档

## 一、系统概述

表情系统是小智 AI 聊天机器人的重要组成部分，通过 GIF 动画或 Font 图标在屏幕上展示设备的情感状态，提升人机交互体验。

## 二、系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                     服务器 (LLM)                             │
│        发送 JSON: {"type":"llm","emotion":"happy"}          │
└─────────────────────────────┬───────────────────────────────┘
                              │ WebSocket
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              Application (application.cc:445-449)            │
│     解析 JSON → 提取 emotion 字段 → 调用 SetEmotion()         │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Display 层 (多态)                          │
│   ┌─────────────┐     ┌──────────────┐    ┌─────────────┐  │
│   │ Display     │     │ LcdDisplay   │    │ OledDisplay │  │
│   │ (Font图标)   │     │ (GIF动画)    │    │ (字符表情)   │  │
│   └─────────────┘     └──────────────┘    └─────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## 三、支持的表情列表

系统支持 21 种文本表情，映射到 9 种 GIF 动画：

| 文本表情 | GIF 动画 | 描述 | 使用场景 |
|---------|---------|------|---------|
| `neutral` | `happy` | 中性/默认 | 空闲、待机 |
| `happy` | `happy` | 开心 | 正面反馈 |
| `laughing` | `happy` | 大笑 | 幽默互动 |
| `funny` | `happy` | 有趣 | 轻松对话 |
| `relaxed` | `happy` | 放松 | 闲聊 |
| `sad` | `sad` | 难过 | 负面消息 |
| `crying` | `sad` | 哭泣 | 悲伤内容 |
| `angry` | `angry` | 生气 | 警告、错误 |
| `loving` | `love` | 爱慕 | 表达喜爱 |
| `kissy` | `love` | 亲吻 | 亲密表达 |
| `embarrassed` | `confused` | 尴尬 | 不确定 |
| `confident` | `confused` | 自信 | 肯定回答 |
| `confused` | `confused` | 困惑 | 不理解 |
| `thinking` | `thinking` | 思考 | 处理中 |
| `surprised` | `delicious` | 惊讶 | 意外发现 |
| `shocked` | `delicious` | 震惊 | 强烈惊讶 |
| `delicious` | `delicious` | 美味 | 食物相关 |
| `silly` | `delicious` | 傻傻的 | 调皮 |
| `winking` | `cool` | 眨眼 | 暗示 |
| `cool` | `cool` | 酷 | 自信表达 |
| `sleepy` | `sleepy` | 困倦 | 省电模式 |

## 四、核心代码实现

### 4.1 消息接收 (application.cc)

服务器通过 WebSocket 发送 JSON 消息：

```cpp
// application.cc:442-449
} else if (strcmp(type->valuestring, "llm") == 0) {
    auto emotion = cJSON_GetObjectItem(root, "emotion");
    if (cJSON_IsString(emotion)) {
        Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
            display->SetEmotion(emotion_str.c_str());
        });
    }
}
```

### 4.2 表情映射表 (lcd_display.cc)

```cpp
// lcd_display.cc:937-964
struct Emotion {
    const lv_img_dsc_t* gif;
    const char* text;
};

#if CONFIG_USE_LCD_240X240_GIF1 || CONFIG_USE_LCD_160X160_GIF1
static const std::vector<Emotion> emotions = {
    {&happy, "neutral"},
    {&happy, "happy"},
    {&happy, "laughing"},
    {&happy, "funny"},
    {&sad, "sad"},
    {&angry, "angry"},
    {&sad, "crying"},
    {&love, "loving"},
    {&confused, "embarrassed"},
    {&delicious, "surprised"},
    {&delicious, "shocked"},
    {&thinking, "thinking"},
    {&cool, "winking"},
    {&cool, "cool"},
    {&happy, "relaxed"},
    {&delicious, "delicious"},
    {&love, "kissy"},
    {&confused, "confident"},
    {&sleepy, "sleepy"},
    {&delicious, "silly"},
    {&confused, "confused"}
};
#endif
```

### 4.3 SetEmotion 实现

```cpp
// lcd_display.cc:934-1021
void LcdDisplay::SetEmotion(const char* emotion) {
    // 1. 字符串查找匹配
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });

    // 2. 加锁保护 LVGL 操作
    DisplayLockGuard lock(this);

    // 3. 检查 gif_label_ 是否有效
    if (gif_label_ == nullptr) {
        ESP_LOGW(TAG, "SetEmotion: gif_label_ is NULL");
        return;
    }

    // 4. 设置 GIF 源
    if (it != emotions.end()) {
        lv_gif_set_src(gif_label_, it->gif);
    } else {
        lv_gif_set_src(gif_label_, &happy);  // 默认
    }
}
```

## 五、两种显示模式

### 5.1 LcdDisplay (GIF 动画)

- **数据类型**: `lv_img_dsc_t*` (LVGL 图像描述符)
- **LVGL 组件**: `lv_gif` (GIF 播放器)
- **视觉效果**: 丰富的帧动画
- **资源占用**: 较大 (~500KB GIF 数据)
- **适用场景**: 240x240 / 160x160 圆形 LCD

### 5.2 Display (Font 图标)

- **数据类型**: `const char*` (FontAwesome Unicode)
- **LVGL 组件**: `lv_label` (文本标签)
- **视觉效果**: 静态图标
- **资源占用**: 较小 (字体文件)
- **适用场景**: 小屏幕、资源受限设备

## 六、表情触发场景

| 场景 | 代码位置 | 表情 | 说明 |
|------|---------|------|------|
| 启动完成 | `application.cc:653` | `neutral` | 进入空闲状态 |
| 连接服务器 | `application.cc:659` | `neutral` | 建立连接中 |
| 语音监听 | `application.cc:664` | `neutral` | 等待用户说话 |
| 省电模式开启 | `display.cc:263` | `sleepy` | 降低功耗 |
| 省电模式关闭 | `display.cc:265` | `neutral` | 恢复正常 |
| LLM 响应 | `application.cc:448` | 服务器指定 | 根据对话内容 |
| Alert 警告 | `application.cc:213` | 自定义 | 错误或提示 |

## 七、协议格式

### 7.1 LLM 表情消息

```json
{
    "type": "llm",
    "emotion": "happy"
}
```

### 7.2 Alert 消息

```json
{
    "type": "alert",
    "status": "警告",
    "message": "电量不足",
    "emotion": "sad"
}
```

### 7.3 自定义消息 (需启用 CONFIG_RECEIVE_CUSTOM_MESSAGE)

```json
{
    "type": "custom",
    "emotion": "thinking",
    "text": "处理中..."
}
```

## 八、GIF 资源文件

```
main/display/assets/
├── 240x240_gif1/           # zhengchen-eye 使用
│   ├── happy.c             # 开心动画 (默认)
│   ├── sad.c               # 难过动画
│   ├── angry.c             # 生气动画
│   ├── love.c              # 爱心动画
│   ├── confused.c          # 困惑动画
│   ├── delicious.c         # 惊讶/美味动画
│   ├── cool.c              # 酷动画
│   ├── sleepy.c            # 困倦动画
│   └── thinking.c          # 思考动画
├── 240x240_gif2/           # 备选表情集
├── 160x160_gif1/           # 小尺寸版本
└── 160x160_gif2/           # 小尺寸备选
```

### GIF 数据格式

每个 `.c` 文件包含 LVGL 专用的图像描述符：

```c
const lv_img_dsc_t happy = {
    .header = {
        .cf = LV_IMG_CF_RAW,
        .w = 240,
        .h = 240,
    },
    .data_size = 12345,
    .data = happy_gif_data,  // GIF 帧数据
};
```

## 九、配置选项

在 `sdkconfig` 中配置：

```ini
# 240x240 GIF 表情集 1 (推荐)
CONFIG_USE_LCD_240X240_GIF1=y

# 或 160x160 GIF 表情集
# CONFIG_USE_LCD_160X160_GIF1=y

# 或 GIF 表情集 2
# CONFIG_USE_LCD_240X240_GIF2=y
```

## 十、调试方法

### 10.1 日志级别

```cpp
// 设置调试日志
ESP_LOGD(TAG, "SetEmotion called: '%s'", emotion);
```

### 10.2 串口监控

```bash
# 监控表情变化
python3 -c "
import serial
ser = serial.Serial('/dev/ttyACM0', 115200)
while True:
    line = ser.readline().decode('utf-8', errors='ignore')
    if 'SetEmotion' in line or 'emotion' in line:
        print(line, end='')
"
```

## 十一、扩展开发

### 11.1 添加新表情

1. 准备 GIF 文件 (240x240 或 160x160)
2. 使用 LVGL 工具转换为 C 数组
3. 添加到 `assets/240x240_gif1/`
4. 在 `lcd_display.cc` 中声明和映射

### 11.2 通过 WebSocket 发送表情

```python
import websocket
import json

ws = websocket.create_connection("ws://device_ip:port/ws")
ws.send(json.dumps({
    "type": "llm",
    "emotion": "happy"
}))
```

---

**文档版本**: 1.0
**最后更新**: 2025-12-19
**维护者**: Claude Code
