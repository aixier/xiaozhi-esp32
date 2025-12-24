#include "dual_network_board.h"
#include "display/lcd_display.h"
#include "audio/codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "config.h"
#include "assets/lang_config.h"
#include "font_awesome_symbols.h"

#include <esp_lcd_panel_vendor.h>
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include "power_save_timer.h"
#include "../zhengchen-1.54tft-wifi/power_manager.h"
#include "driver/touch_pad.h"

#if CONFIG_USE_LCD_240X240_GIF1 || CONFIG_USE_LCD_240X240_GIF2
#include <esp_lcd_gc9a01.h>
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};

#else
#include "esp_lcd_gc9d01n.h"
#endif

#define TAG "zhengchen_eye"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);


class zhengchen_eye : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    uint32_t touch_value = 0;
    uint32_t touch_value1 = 0;


    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_7);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
                ESP_LOGW(TAG, "Charging, disable power save timer");
            } else {
                power_save_timer_->SetEnabled(true);
                ESP_LOGW(TAG, "Not charging, enable power save timer");
            }
        });
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(240, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
         
        power_save_timer_->SetEnabled(true);
    }

    void touch_init() {
        touch_pad_init();
        touch_pad_config(TOUCH_PAD_NUM4); // 配置 GPIO4 为触摸引脚
        touch_pad_config(TOUCH_PAD_NUM5); // 配置 GPIO5 为触摸引脚
        touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER); // 设置 FSM 模式为定时器模式
        touch_pad_fsm_start();
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }

    static void touch_read_task(void* arg) {
        zhengchen_eye* self = static_cast<zhengchen_eye*>(arg);
        auto& app = Application::GetInstance();
        while (1) {
            touch_pad_read_raw_data(TOUCH_PAD_NUM4, &self->touch_value);
            touch_pad_read_raw_data(TOUCH_PAD_NUM5, &self->touch_value1);
            if (self->touch_value > 30000) {
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    app.WakeWordInvoke("(正在抚摸你的头，请提供相关的情绪价值，回答)");
                }
            }

            if (self->touch_value1 > 30000) {
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    app.WakeWordInvoke("(正在抚摸你的身体，请提供相关的情绪价值，回答)");
                }
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
   
    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

   


    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            ESP_LOGI("Button", "[CLICK] >> Button pressed, getting app state...");
            auto& app = Application::GetInstance();
            auto state = app.GetDeviceState();
            ESP_LOGI("Button", "[CLICK] Device state=%d", state);
            if (GetNetworkType() == NetworkType::WIFI) {
                if (state == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }
            ESP_LOGI("Button", "[CLICK] Calling ToggleChatState...");
            app.ToggleChatState();
            ESP_LOGI("Button", "[CLICK] << ToggleChatState returned");
        });
#if CONFIG_USE_4G_WIFI
        boot_button_.OnMultipleClick([this]() {
            SwitchNetworkType();
        });
#endif
        boot_button_.OnLongPress([this]() {
            if (GetNetworkType() == NetworkType::WIFI) {
                auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                wifi_board.ResetWifiConfiguration();
            }
            
        });

#if CONFIG_USE_DEVICE_AEC
        ESP_LOGW(TAG, "4GGGGGGGG");
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            
        });
#endif
    }


    void InitializeSpi() {
        ESP_LOGI(TAG, "=== SPI Bus Init Start ===");
        ESP_LOGI(TAG, "SPI pins: MOSI=%d, SCLK=%d", DISPLAY_SDA, DISPLAY_SCL);
        ESP_LOGI(TAG, "Display size: %dx%d, buffer=%d bytes",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));

        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

        esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus init FAILED: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "SPI bus init OK");
        }
        ESP_ERROR_CHECK(ret);
    }

    void InitializeGc9107Display(){
        ESP_LOGI(TAG, "=== LCD Display Init Start ===");

        // 液晶屏控制IO初始化
        ESP_LOGI(TAG, "Creating LCD panel IO, DC pin=%d, PCLK=40MHz", DISPLAY_DC);
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = -1;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;

        esp_err_t ret = esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LCD panel IO create FAILED: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "LCD panel IO create OK, handle=%p", panel_io);
        }
        ESP_ERROR_CHECK(ret);

#if CONFIG_USE_LCD_240X240_GIF1 || CONFIG_USE_LCD_240X240_GIF2
       // 初始化液晶屏驱动芯片 GC9A01/GC9107
       ESP_LOGI(TAG, "Installing GC9A01/GC9107 LCD driver, RST pin=%d", DISPLAY_RES);
       esp_lcd_panel_dev_config_t panel_config = {};
       panel_config.reset_gpio_num =DISPLAY_RES;
       panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
       panel_config.bits_per_pixel = 16;

       ret = esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel);
       if (ret != ESP_OK) {
           ESP_LOGE(TAG, "GC9A01 panel create FAILED: %s", esp_err_to_name(ret));
       } else {
           ESP_LOGI(TAG, "GC9A01 panel create OK, handle=%p", panel);
       }
       ESP_ERROR_CHECK(ret);

       gc9a01_vendor_config_t gc9107_vendor_config = {
           .init_cmds = gc9107_lcd_init_cmds,
           .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
       };

       ESP_LOGI(TAG, "Resetting LCD panel...");
       esp_lcd_panel_reset(panel);
       ESP_LOGI(TAG, "Initializing LCD panel...");
       esp_lcd_panel_init(panel);
       ESP_LOGI(TAG, "Configuring LCD: invert=true, swap_xy=%d, mirror_x=%d, mirror_y=%d",
                DISPLAY_SWAP_XY, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
       esp_lcd_panel_invert_color(panel, true);
       esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
       esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
       panel_config.vendor_config = &gc9107_vendor_config;
       ESP_LOGI(TAG, "GC9A01/GC9107 LCD driver init complete");
#else
        // 初始化液晶屏驱动芯片 GC9D01N
        ESP_LOGI(TAG, "Installing GC9D01N LCD driver, RST pin=%d", DISPLAY_RES);
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;

        ret = esp_lcd_new_panel_gc9d01n(panel_io, &panel_config, &panel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GC9D01N panel create FAILED: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "GC9D01N panel create OK, handle=%p", panel);
        }
        ESP_ERROR_CHECK(ret);

        ESP_LOGI(TAG, "Resetting LCD panel...");
        esp_lcd_panel_reset(panel);
        ESP_LOGI(TAG, "Initializing LCD panel...");
        esp_lcd_panel_init(panel);
        ESP_LOGI(TAG, "Configuring LCD: invert=false, swap_xy=%d, mirror_x=%d, mirror_y=%d",
                 DISPLAY_SWAP_XY, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        ESP_LOGI(TAG, "GC9D01N LCD driver init complete");
#endif

        ESP_LOGI(TAG, "Creating SpiLcdDisplay: %dx%d, offset=(%d,%d)",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                .text_font = &font_puhui_16_4,
                .icon_font = &font_awesome_16_4,
                .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
            });
        ESP_LOGI(TAG, "SpiLcdDisplay created, display_=%p", display_);
        ESP_LOGI(TAG, "=== LCD Display Init Complete ===");
    }

public:
    zhengchen_eye() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN),
        boot_button_(BOOT_BUTTON_GPIO){
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "zhengchen_eye board initialization START");
        ESP_LOGI(TAG, "========================================");

        ESP_LOGI(TAG, "[1/7] Initializing Power Manager...");
        InitializePowerManager();
        ESP_LOGI(TAG, "[1/7] Power Manager OK");

        ESP_LOGI(TAG, "[2/7] Initializing Power Save Timer...");
        InitializePowerSaveTimer();
        ESP_LOGI(TAG, "[2/7] Power Save Timer OK");

        ESP_LOGI(TAG, "[3/7] Initializing Codec I2C...");
        InitializeCodecI2c();
        ESP_LOGI(TAG, "[3/7] Codec I2C OK");

        ESP_LOGI(TAG, "[4/7] Initializing Buttons...");
        InitializeButtons();
        ESP_LOGI(TAG, "[4/7] Buttons OK");

#if !CONFIG_USE_NOLCD
        ESP_LOGI(TAG, "[5/7] Initializing SPI for display...");
        InitializeSpi();

        ESP_LOGI(TAG, "[6/7] Initializing LCD Display...");
        InitializeGc9107Display();

        ESP_LOGI(TAG, "[6.1/7] Setting backlight to 100%%...");
        GetBacklight()->SetBrightness(100);
        ESP_LOGI(TAG, "[6.1/7] Backlight set OK");
#else
        ESP_LOGI(TAG, "[5-6/7] LCD disabled (CONFIG_USE_NOLCD)");
#endif

        ESP_LOGI(TAG, "[7/7] Initializing Touch...");
        touch_init();
        ESP_LOGI(TAG, "[7/7] Touch OK");

        ESP_LOGI(TAG, "Setting audio codec output volume to 100%%...");
        GetAudioCodec()->SetOutputVolume(100);

        ESP_LOGI(TAG, "Creating touch read task...");
        xTaskCreate(touch_read_task, "touch_read_task", 2048, this, 5, NULL);

        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "zhengchen_eye board initialization DONE");
        ESP_LOGI(TAG, "========================================");
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }
    
#if !CONFIG_USE_NOLCD
    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
#endif

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }
  

    virtual bool GetTemperature(float& esp32temp)  override {
        esp32temp = power_manager_->GetTemperature();
        return true;
    }

    virtual bool Gethead_value(uint32_t& head_value)  override {
        head_value = touch_value;
        printf("Touch1 value: %ld\n", touch_value);
        return true;
    }

    virtual bool Getbody_value(uint32_t& body_value)  override {
        body_value = touch_value1;
        printf("Touch2 value: %ld\n", touch_value1);
        return true;
    }
};

DECLARE_BOARD(zhengchen_eye);
