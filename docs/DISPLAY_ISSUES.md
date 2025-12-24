# 显示相关问题文档

## 问题 1: Alert 消息不显示 (虚函数调度失败)

**日志特征**：
```
W (xxx) Display: BASE SetChatMessage called! role=system, content=xxx
```

**根因**：`LcdDisplay::SetChatMessage` 被条件编译包裹，未启用时回退到基类空实现。

**解决方案**：在 `config.json` 添加 `"CONFIG_USE_WECHAT_MESSAGE_STYLE=y"`

---

## 问题 2: 表情动画不显示 (WECHAT_MESSAGE_STYLE 与 GIF 冲突)

**现象**：通过 Emotion API 发送表情，设备收到消息但屏幕无变化。

**日志特征**：
```
W (xxx) LCD: SetEmotion: gif_label_ is NULL, cannot set emotion
```

### 根因分析

`lcd_display.cc` 有两套 `SetupUI()` 实现：

| 模式 | 条件 | 创建的组件 | 表情显示 |
|------|------|-----------|---------|
| WECHAT 模式 | `CONFIG_USE_WECHAT_MESSAGE_STYLE=y` | `emotion_label_` (图标) | ❌ 不支持 GIF |
| GIF 模式 | 未设置 | `gif_label_` (GIF动画) | ✅ 全屏 GIF |

### UI 层级对比

```
# WECHAT 模式 (lcd_display.cc:355-463)
container_
├── status_bar_
│   └── emotion_label_  ← 只有图标，无 GIF
└── content_  ← 聊天气泡区域

# GIF 模式 (lcd_display.cc:761-912)
container_
├── overlay_container
│   └── gif_label_  ← GIF 动画
├── content_
└── status_bar_
```

### SetEmotion 执行流程

```cpp
// lcd_display.cc:1003-1006
if (gif_label_ == nullptr) {  // WECHAT模式下为 NULL
    ESP_LOGW(TAG, "SetEmotion: gif_label_ is NULL, cannot set emotion");
    return;  // 直接返回，静默失败
}
```

### 相关代码位置

| 文件 | 行号 | 说明 |
|------|------|------|
| `lcd_display.cc` | 355-463 | WECHAT SetupUI |
| `lcd_display.cc` | 761-912 | GIF SetupUI |
| `lcd_display.cc` | 941-1026 | SetEmotion 函数 |
| `lcd_display.cc` | 1003-1006 | 空指针检查 |

### 可能的解决方案

| 方案 | 说明 | 优点 | 缺点 |
|------|------|------|------|
| A | WECHAT模式添加GIF支持 | 完整 GIF 动画 | 需处理 content_ 层级遮盖 |
| B | SetEmotion 使用备用图标 | 改动小 | 只能显示图标非动画 |
| C | 禁用 WECHAT 模式 | 立即可用 | 失去聊天气泡样式 |

**注意**：此问题不会导致设备崩溃，有空指针保护。
