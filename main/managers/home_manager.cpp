#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "home_manager.h"

#define NVS_NAMESPACE "home_manager"
#define NVS_KEY "home"

static const char *TAG = "home_manager";

void home_manager_init(home_manager_t *manager)
{
    if (manager == NULL)
    {
        ESP_LOGE(TAG, "Manager pointer is NULL!");
        return;
    }

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    memset(manager, 0, sizeof(home_manager_t));

    manager->outdoor_temperature = 0;

    esp_err_t load_err = load_home_from_nvs(manager);

    if (load_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded home from NVS");
    }
    else
    {
        ESP_LOGW(TAG, "No home found in NVS (err: 0x%x)", load_err);
    }
}

esp_err_t load_home_from_nvs(home_manager_t *manager)
{
    return ESP_OK;
}

esp_err_t save_home_to_nvs(home_manager_t *manager)
{
    return ESP_OK;
}