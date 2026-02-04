#include <stdio.h>
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include <esp_matter.h>

#include <inttypes.h>

class StatusDisplay
{
public:
    StatusDisplay() 
    {
        ESP_LOGI("StatusDisplay", "StatusDisplay::StatusDisplay()");
    }

    esp_err_t Init();

    void UpdateDisplay(uint16_t outdoor_temperature);

private:
    friend StatusDisplay & StatusDisplayMgr(void);
    static StatusDisplay sStatusDisplay;

    bool mScreenOn = false;

    char *mCommissioningCode;

    lv_disp_t *mDisplayHandle;
    esp_lcd_panel_handle_t mPanelHandle;

    lv_obj_t *mOutdoorTemperatureLabel;
    
    uint16_t mOutdoorTemperature = 0;
};

inline StatusDisplay & StatusDisplayMgr(void)
{
    return StatusDisplay::sStatusDisplay;
}