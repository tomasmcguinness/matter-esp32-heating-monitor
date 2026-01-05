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
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, NVS_KEY, NULL, &required_size);
    if (err != ESP_OK || required_size == 0)
    {
        nvs_close(nvs_handle);
        return err;
    }

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(nvs_handle, NVS_KEY, buffer, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        free(buffer);
        return err;
    }

    uint8_t *ptr = buffer;

    manager->outdoor_temp_node_id = *((uint64_t *)ptr);
    ptr += sizeof(uint64_t);

    manager->outdoor_temp_endpoint_id = *((uint16_t *)ptr);
    ptr += sizeof(uint16_t);

    free(buffer);

    return ESP_OK;
}

esp_err_t save_home_to_nvs(home_manager_t *manager)
{
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t required_size = sizeof(uint64_t); // outdoor_temp_node_id
    required_size += sizeof(uint16_t);       // room_id

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *ptr = buffer;

    *((uint64_t *)ptr) = manager->outdoor_temp_node_id;
    ptr += sizeof(uint64_t);

    *((uint16_t *)ptr) = manager->outdoor_temp_endpoint_id;
    ptr += sizeof(uint16_t);

    err = nvs_set_blob(nvs_handle, NVS_KEY, buffer, required_size);
    free(buffer);

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    return err;
}