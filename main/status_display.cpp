#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "status_display.h"

#include "driver/i2c_master.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

#include "esp_lcd_panel_ssd1681.h"

#include "lvgl.h"

static const char *TAG = "status_display";

#define I2C_BUS_PORT 0
#define LCD_HOST SPI2_HOST

#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

#define LCD_H_RES 400
#define LCD_V_RES 300

#define PIN_NUM_MOSI 11
#define PIN_NUM_MISO 13
#define PIN_NUM_SCLK 12
#define PIN_NUM_CS 10
#define PIN_NUM_DC 46
#define PIN_NUM_RST 47
#define PIN_NUM_BUSY 48
#define PIN_NUM_LCD_POWER 7

StatusDisplay StatusDisplay::sStatusDisplay;

esp_err_t StatusDisplay::Init()
{
    ESP_LOGI(TAG, "StatusDisplay::Init()");

    ESP_LOGI(TAG, "Initialize SPI bus");

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_NUM_CS,
        .dc_gpio_num = PIN_NUM_DC,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 7,
        .on_color_trans_done = NULL,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1683 panel driver");
    esp_lcd_ssd1681_config_t epaper_ssd1681_config = {
        .busy_gpio_num = PIN_NUM_BUSY,
        .non_copy_mode = false,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .flags = {
            .reset_active_high = true,
        },
        .vendor_config = &epaper_ssd1681_config};

    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1681(io_handle, &panel_config, &mPanelHandle));

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_NUM_LCD_POWER);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)PIN_NUM_LCD_POWER, true);
    ESP_LOGI(TAG, "Applied power to display");

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Performing reset...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(mPanelHandle));

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Performing init...");
    ESP_ERROR_CHECK(esp_lcd_panel_init(mPanelHandle));

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    ESP_LOGI(TAG, "Create LVGL Display");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = mPanelHandle,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = false,
        .trans_size = 1024,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_I1,
        .flags = {.buff_dma = false, .buff_spiram = true, .full_refresh = true}};

    mDisplayHandle = lvgl_port_add_disp(&disp_cfg);

    ESP_LOGI(TAG, "Create LVGL Screen");

    lv_obj_t *scr = lv_scr_act();

    LV_FONT_DECLARE(lv_font_montserrat_48);

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);

    static lv_style_t large_style;
    lv_style_init(&large_style);
    lv_style_set_text_font(&large_style, &lv_font_montserrat_48);

    static lv_style_t medium_style;
    lv_style_init(&medium_style);
    lv_style_set_text_font(&medium_style, &lv_font_montserrat_24);

    mOutdoorTemperatureLabel = lv_label_create(scr);

    lv_label_set_text(mOutdoorTemperatureLabel, "0°C");
    lv_obj_set_width(mOutdoorTemperatureLabel, 400);
    lv_obj_align(mOutdoorTemperatureLabel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(mOutdoorTemperatureLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mOutdoorTemperatureLabel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(mOutdoorTemperatureLabel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_add_style(mOutdoorTemperatureLabel, &large_style, LV_PART_MAIN);
    lv_obj_set_style_pad_left(mOutdoorTemperatureLabel, 10, LV_PART_MAIN);
    lv_obj_add_flag(mOutdoorTemperatureLabel, LV_OBJ_FLAG_HIDDEN);

    vTaskDelay(250 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));

    ESP_LOGI(TAG, "StatusDisplay::Init() finished");

    return ESP_OK;
}

void StatusDisplay::UpdateDisplay(uint16_t outdoorTemperature)
{
    ESP_LOGI(TAG, "Updating the display");

    ESP_LOGI(TAG, "outdoorTemperature: [%u]", outdoorTemperature);

    bool shouldRefresh = false;

    bool hasOutdoorTemperatureChanged = mOutdoorTemperature != outdoorTemperature;

    shouldRefresh = hasOutdoorTemperatureChanged;

    ESP_LOGI(TAG, "hasOutdoorTemperatureChanged: [%d]", hasOutdoorTemperatureChanged);
    ESP_LOGI(TAG, "shouldRefresh: [%d]", shouldRefresh);

    if (!shouldRefresh)
    {
        return;
    }

    lvgl_port_lock(0);

    char outdoor_temperature[8];
    snprintf(outdoor_temperature, 8, "%u°C", outdoorTemperature);

    lv_label_set_text(mOutdoorTemperatureLabel, outdoor_temperature);

    lvgl_port_unlock();

    mOutdoorTemperature = outdoorTemperature;

    vTaskDelay(250 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(epaper_panel_refresh_screen(mPanelHandle));
}