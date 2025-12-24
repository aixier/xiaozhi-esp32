#include "lcd_display.h"

#include <vector>
#include <algorithm>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"
#include <libs/gif/lv_gif.h>
#include <cstring>
#include <vector>

#include "board.h"

#define TAG "LcdDisplay"

#if CONFIG_USE_LCD_240X240_GIF1 || CONFIG_USE_LCD_160X160_GIF1
LV_IMG_DECLARE(angry);
LV_IMG_DECLARE(confused);
LV_IMG_DECLARE(cool);
LV_IMG_DECLARE(delicious);
LV_IMG_DECLARE(happy);
LV_IMG_DECLARE(love);
LV_IMG_DECLARE(sad);
LV_IMG_DECLARE(sleepy);
LV_IMG_DECLARE(thinking);
#else
LV_IMG_DECLARE(angry);
LV_IMG_DECLARE(confused);
LV_IMG_DECLARE(happy);
LV_IMG_DECLARE(love);
LV_IMG_DECLARE(neutral);
LV_IMG_DECLARE(sleepy);
LV_IMG_DECLARE(thinking);
LV_IMG_DECLARE(winking);
#endif


// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR       lv_color_hex(0x121212)     // Dark background
#define DARK_TEXT_COLOR             lv_color_white()           // White text
#define DARK_CHAT_BACKGROUND_COLOR  lv_color_hex(0x1E1E1E)     // Slightly lighter than background
#define DARK_USER_BUBBLE_COLOR      lv_color_hex(0x1A6C37)     // Dark green
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333)     // Dark gray
#define DARK_SYSTEM_BUBBLE_COLOR    lv_color_hex(0x2A2A2A)     // Medium gray
#define DARK_SYSTEM_TEXT_COLOR      lv_color_hex(0xAAAAAA)     // Light gray text
#define DARK_BORDER_COLOR           lv_color_hex(0x333333)     // Dark gray border
#define DARK_LOW_BATTERY_COLOR      lv_color_hex(0xFF0000)     // Red for dark mode

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR       lv_color_white()           // White background
#define LIGHT_TEXT_COLOR             lv_color_black()           // Black text
#define LIGHT_CHAT_BACKGROUND_COLOR  lv_color_hex(0xE0E0E0)     // Light gray background
#define LIGHT_USER_BUBBLE_COLOR      lv_color_hex(0x95EC69)     // WeChat green
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_white()           // White
#define LIGHT_SYSTEM_BUBBLE_COLOR    lv_color_hex(0xE0E0E0)     // Light gray
#define LIGHT_SYSTEM_TEXT_COLOR      lv_color_hex(0x666666)     // Dark gray text
#define LIGHT_BORDER_COLOR           lv_color_hex(0xE0E0E0)     // Light gray border
#define LIGHT_LOW_BATTERY_COLOR      lv_color_black()           // Black for light mode


// Define dark theme colors
const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR,
    .text = DARK_TEXT_COLOR,
    .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR,
    .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR,
    .border = DARK_BORDER_COLOR,
    .low_battery = DARK_LOW_BATTERY_COLOR
};

// Define light theme colors
const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR,
    .text = LIGHT_TEXT_COLOR,
    .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR,
    .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR,
    .border = LIGHT_BORDER_COLOR,
    .low_battery = LIGHT_LOW_BATTERY_COLOR
};


LV_FONT_DECLARE(font_awesome_30_4);

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, DisplayFonts fonts, int width, int height)
    : panel_io_(panel_io), panel_(panel), fonts_(fonts) {
    width_ = width;
    height_ = height;

    // Load theme from settings
    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light");

    // Update the theme
    if (current_theme_name_ == "dark") {
        current_theme_ = DARK_THEME;
    } else if (current_theme_name_ == "light") {
        current_theme_ = LIGHT_THEME;
    }
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                           DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height) {

    ESP_LOGI(TAG, "=== SpiLcdDisplay Constructor Start ===");
    ESP_LOGI(TAG, "Parameters: %dx%d, offset=(%d,%d), mirror=(%d,%d), swap_xy=%d",
             width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy);

    // draw white - clear screen
    ESP_LOGI(TAG, "Clearing screen with white...");
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }
    ESP_LOGI(TAG, "Screen cleared");

    // Set the display to on
    ESP_LOGI(TAG, "Turning display panel ON...");
    esp_err_t ret = esp_lcd_panel_disp_on_off(panel_, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display ON FAILED: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Display ON OK");
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing LVGL library (lv_init)...");
    lv_init();
    ESP_LOGI(TAG, "LVGL library initialized");

    ESP_LOGI(TAG, "Initializing LVGL port (priority=1, timer=50ms)...");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    esp_err_t port_ret = lvgl_port_init(&port_cfg);
    if (port_ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init FAILED: %s", esp_err_to_name(port_ret));
    } else {
        ESP_LOGI(TAG, "LVGL port init OK");
    }

    ESP_LOGI(TAG, "Adding LCD display to LVGL, buffer=%d bytes, DMA=%d",
             width_ * 20 * sizeof(uint16_t), 1);
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "LVGL add display FAILED - display_ is NULL!");
        return;
    }
    ESP_LOGI(TAG, "LVGL display added OK, display_=%p", display_);

    if (offset_x != 0 || offset_y != 0) {
        ESP_LOGI(TAG, "Setting display offset: (%d, %d)", offset_x, offset_y);
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    ESP_LOGI(TAG, "Calling SetupUI()...");
    SetupUI();
    ESP_LOGI(TAG, "=== SpiLcdDisplay Constructor Complete ===");
}

// RGB LCD实现
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy,
                           DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };
    
    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }
    
    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height,  int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy,
                            DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height) {

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = panel_io,
            .panel_handle = panel,
            .control_handle = nullptr,
            .buffer_size = static_cast<uint32_t>(width_ * 50),
            .double_buffer = false,
            .hres = static_cast<uint32_t>(width_),
            .vres = static_cast<uint32_t>(height_),
            .monochrome = false,
            /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
            .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram =false,
            .sw_rotate = false,
        },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

LcdDisplay::~LcdDisplay() {
    // 然后再清理 LVGL 对象
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void LcdDisplay::Unlock() {
    lvgl_port_unlock();
}

void LcdDisplay::SetupUI() {
    ESP_LOGI(TAG, "=== SetupUI Start (GIF Style) ===");
    DisplayLockGuard lock(this);

    ESP_LOGI(TAG, "Getting active screen...");
    auto screen = lv_screen_active();
    if (screen == nullptr) {
        ESP_LOGE(TAG, "lv_screen_active() returned NULL!");
        return;
    }
    ESP_LOGI(TAG, "Active screen=%p", screen);

    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);

    /* Container */
    ESP_LOGI(TAG, "Creating container...");
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_border_color(container_, current_theme_.border, 0);
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    ESP_LOGI(TAG, "Container created, container_=%p", container_);


    /* Status bar */
    ESP_LOGI(TAG, "Creating status bar...");
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Status bar created, status_bar_=%p", status_bar_);

    ESP_LOGI(TAG, "Creating overlay container for GIF...");
    // 创建在 screen 上，而不是 container_ 上，这样可以独立控制显示/隐藏
    overlay_container = lv_obj_create(screen);
    lv_obj_remove_style_all(overlay_container);
    lv_obj_set_size(overlay_container, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(overlay_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay_container, LV_OPA_COVER, 0);
    lv_obj_align(overlay_container, LV_ALIGN_CENTER, 0, 0);
    ESP_LOGI(TAG, "Overlay container created, overlay_container=%p", overlay_container);


    ESP_LOGI(TAG, "Creating GIF label (lv_gif_create)...");
    gif_label_ = lv_gif_create(overlay_container);
    if (gif_label_ == nullptr) {
        ESP_LOGE(TAG, "lv_gif_create FAILED - gif_label_ is NULL!");
    } else {
        ESP_LOGI(TAG, "GIF label created, gif_label_=%p", gif_label_);
    }

#if CONFIG_USE_LCD_240X240_GIF1 || CONFIG_USE_LCD_160X160_GIF1
    ESP_LOGI(TAG, "Setting initial GIF source: happy (240x240/160x160 GIF1 config)");
    lv_gif_set_src(gif_label_, &happy);
#else
    ESP_LOGI(TAG, "Setting initial GIF source: neutral (default config)");
    lv_gif_set_src(gif_label_, &neutral);
#endif
    lv_obj_set_size(gif_label_, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(gif_label_, LV_ALIGN_CENTER, 0, 0);
    ESP_LOGI(TAG, "GIF initialized, size=%dx%d", (int)LV_HOR_RES, (int)LV_VER_RES); 
   
    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 5, 0);
    lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0);
    lv_obj_set_style_border_color(content_, current_theme_.border, 0); // Border color for content

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY); // 子对象居中对齐，等距分布

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);

    preview_image_ = lv_image_create(content_);
    lv_obj_set_size(preview_image_, width_ * 0.5, height_ * 0.5);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9); // 限制宽度为屏幕宽度的 90%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP); // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐
    lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);

    ESP_LOGI(TAG, "Creating low battery popup...");
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Low battery popup created");

    // 初始化显示模式为表情模式（显示 GIF）
    SetDisplayMode(kDisplayModeEmotion);

    ESP_LOGI(TAG, "=== SetupUI Complete (GIF Style) ===");
}

void LcdDisplay::SetPreviewImage(const lv_img_dsc_t* img_dsc) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        return;
    }
    
    if (img_dsc != nullptr) {
        // zoom factor 0.5
        lv_image_set_scale(preview_image_, 128 * width_ / img_dsc->header.w);
        // 设置图片源并显示预览图片
        lv_image_set_src(preview_image_, img_dsc);
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        // 隐藏emotion_label_
        if (emotion_label_ != nullptr) {
            lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // 隐藏预览图片并显示emotion_label_
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        if (emotion_label_ != nullptr) {
            lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


void LcdDisplay::SetEmotion(const char* emotion) {
    ESP_LOGI(TAG, ">>> LcdDisplay::SetEmotion called: '%s'", emotion ? emotion : "NULL");

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
#else
    static const std::vector<Emotion> emotions = {
        {&neutral, "neutral"},
        {&happy, "happy"},
        {&happy, "laughing"},
        {&happy, "funny"},
        {&neutral, "sad"},
        {&angry, "angry"},
        {&neutral, "crying"},
        {&love, "loving"},
        {&confused, "embarrassed"},
        {&confused, "surprised"},
        {&winking, "shocked"},
        {&confused, "thinking"},
        {&winking, "winking"},
        {&winking, "cool"},
        {&happy, "relaxed"},
        {&winking, "delicious"},
        {&love, "kissy"},
        {&confused, "confident"},
        {&sleepy, "sleepy"},
        {&neutral, "silly"},
        {&confused, "confused"}
    };
#endif
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });

    DisplayLockGuard lock(this);
    if (gif_label_ == nullptr) {
        ESP_LOGW(TAG, "SetEmotion: gif_label_ is NULL, cannot set emotion");
        return;
    }

    if (it != emotions.end()) {
        ESP_LOGD(TAG, "SetEmotion: found matching emotion '%s', setting GIF src=%p",
                 it->text, it->gif);
        lv_gif_set_src(gif_label_, it->gif);
    } else {
        ESP_LOGW(TAG, "SetEmotion: unknown emotion '%s', using default", emotion);
#if CONFIG_USE_LCD_240X240_GIF1 || CONFIG_USE_LCD_160X160_GIF1
        lv_gif_set_src(gif_label_, &happy);
#else
        lv_gif_set_src(gif_label_, &neutral);
#endif
    } 

    // 显示GIF表情，隐藏preview_image_
    lv_obj_clear_flag(gif_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_label_set_text(emotion_label_, icon);

#if !CONFIG_USE_WECHAT_MESSAGE_STYLE
    // 显示emotion_label_，隐藏preview_image_
    lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void LcdDisplay::SetDisplayMode(DisplayMode mode) {
    DisplayLockGuard lock(this);
    display_mode_ = mode;

    if (mode == kDisplayModeChat) {
        // Show Chat UI
        if (container_ != nullptr) lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        // Hide Emotion UI
        if (overlay_container != nullptr) lv_obj_add_flag(overlay_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Hide Chat UI
        if (container_ != nullptr) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        // Show Emotion UI
        if (overlay_container != nullptr) lv_obj_clear_flag(overlay_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::SetTheme(const std::string& theme_name) {
    DisplayLockGuard lock(this);
    
    if (theme_name == "dark" || theme_name == "DARK") {
        current_theme_ = DARK_THEME;
    } else if (theme_name == "light" || theme_name == "LIGHT") {
        current_theme_ = LIGHT_THEME;
    } else {
        // Invalid theme name, return false
        ESP_LOGE(TAG, "Invalid theme name: %s", theme_name.c_str());
        return;
    }
    
    // Get the active screen
    lv_obj_t* screen = lv_screen_active();
    
    // Update the screen colors
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    
    // Update container colors
    if (container_ != nullptr) {
        lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
        lv_obj_set_style_border_color(container_, current_theme_.border, 0);
    }
    
    // Update status bar colors
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
        lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
        
        // Update status bar elements
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
        }
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);
        }
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
        }
        if (emotion_label_ != nullptr) {
            lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
        }
    }
    
    // Update content area colors
    if (content_ != nullptr) {
        lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0);
        lv_obj_set_style_border_color(content_, current_theme_.border, 0);
        
        // Update all message bubbles
        uint32_t child_count = lv_obj_get_child_cnt(content_);
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* obj = lv_obj_get_child(content_, i);
            if (obj == nullptr) continue;
            
            lv_obj_t* bubble = nullptr;
            
            if (lv_obj_get_child_cnt(obj) > 0) {
                lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
                if (bg_opa == LV_OPA_TRANSP) {
                    bubble = lv_obj_get_child(obj, 0);
                } else {
                    bubble = obj;
                }
            } else {
                continue;
            }
            
            if (bubble == nullptr) continue;
            
            void* bubble_type_ptr = lv_obj_get_user_data(bubble);
            if (bubble_type_ptr != nullptr) {
                const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
                
                if (strcmp(bubble_type, "user") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.user_bubble, 0);
                } else if (strcmp(bubble_type, "assistant") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.assistant_bubble, 0); 
                } else if (strcmp(bubble_type, "system") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                } else if (strcmp(bubble_type, "image") == 0) {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                }
                
                lv_obj_set_style_border_color(bubble, current_theme_.border, 0);
                
                if (lv_obj_get_child_cnt(bubble) > 0) {
                    lv_obj_t* text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr) {
                        if (strcmp(bubble_type, "system") == 0) {
                            lv_obj_set_style_text_color(text, current_theme_.system_text, 0);
                        } else {
                            lv_obj_set_style_text_color(text, current_theme_.text, 0);
                        }
                    }
                }
            }
        }
    }
    
    // Update simple UI elements if they exist
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
    }
    
    if (emotion_label_ != nullptr) {
        lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
    }
    
    // Update low battery popup
    if (low_battery_popup_ != nullptr) {
        lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    }

    // No errors occurred. Save theme to settings
    Display::SetTheme(theme_name);
}