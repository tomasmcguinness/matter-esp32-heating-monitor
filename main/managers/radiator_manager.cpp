#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "radiator_manager.h"

#define NVS_NAMESPACE "radiators"
#define NVS_KEY "radiator_list"

static const char *TAG = "radiator_manager";

void radiator_manager_init(radiator_manager_t *manager)
{
    if (manager == NULL)
    {
        ESP_LOGE(TAG, "manager pointer is NULL!");
        return;
    }

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    memset(manager, 0, sizeof(radiator_manager_t));

    esp_err_t load_err = load_radiators_from_nvs(manager);

    if (load_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded %d radiators from NVS", manager->radiator_count);
    }
    else
    {
        ESP_LOGW(TAG, "No saved radiators found in NVS (err: 0x%x)", load_err);
    }
}

radiator_t *find_radiator(radiator_manager_t *manager, uint8_t radiator_id)
{
    radiator_t *current = manager->radiator_list;

    while (current != NULL)
    {
        if (current->radiator_id == radiator_id)
        {
            return current;
        }

        current = current->next;
    }

    return NULL;
}

radiator_t *add_radiator(radiator_manager_t *manager, char *name, uint8_t type, uint16_t outputAtDelta50, uint64_t flowNodeId, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId)
{
    uint8_t new_radiator_id = manager->radiator_count + 1;

    radiator_t *new_radiator = (radiator_t *)calloc(1, sizeof(radiator_t));

    if (!new_radiator)
    {
        return NULL;
    }

    memset(new_radiator, 0, sizeof(radiator_t));

    new_radiator->radiator_id = new_radiator_id;
    new_radiator->name = name;
    new_radiator->type = type;
    new_radiator->outputAtDelta50 = outputAtDelta50;
    new_radiator->flow_temp_nodeId = flowNodeId;
    new_radiator->flow_temp_endpointId = flowEndpointId;
    new_radiator->return_temp_nodeId = returnNodeId;
    new_radiator->return_temp_endpointId = returnEndpointId;

    new_radiator->next = manager->radiator_list;

    manager->radiator_list = new_radiator;
    manager->radiator_count++;

    return new_radiator;
}

void radiator_manager_free(radiator_manager_t *manager)
{
    radiator_t *current = manager->radiator_list;

    while (current != NULL)
    {
        radiator_t *next = current->next;

        free(current);

        current = next;
    }

    manager->radiator_list = NULL;
    manager->radiator_count = 0;
}

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager)
{
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    radiator_manager_free(manager);

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
    uint8_t radiator_count = *((uint8_t *)ptr);
    ptr += sizeof(uint8_t);

    for (uint16_t i = 0; i < radiator_count; i++)
    {
        radiator_t *radiator = (radiator_t *)calloc(1, sizeof(radiator_t));
        if (!radiator)
        {
            free(buffer);
            return ESP_ERR_NO_MEM;
        }

        radiator->radiator_id = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        radiator->name_len = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        ESP_LOGI(TAG, "Radiator name length is %u", radiator->name_len);

        radiator->name = (char *)calloc(radiator->name_len + 1, sizeof(char));

        memcpy(radiator->name, ptr, radiator->name_len);
        ptr += radiator->name_len;

        ESP_LOGI(TAG, "Radiator name is %s", radiator->name);

        radiator->type = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        radiator->outputAtDelta50 = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        radiator->flow_temp_nodeId = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        radiator->flow_temp_endpointId = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        radiator->return_temp_nodeId = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        radiator->return_temp_endpointId = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        ESP_LOGI(TAG, "Loaded radiator 0x%0X from NVS", radiator->radiator_id);

        radiator->next = manager->radiator_list;
        manager->radiator_list = radiator;
    }

    manager->radiator_count = radiator_count;
    free(buffer);

    return ESP_OK;
}

esp_err_t remove_radiator(radiator_manager_t *controller, uint8_t radiator_id)
{
    if (!controller)
    {
        ESP_LOGE(TAG, "Invalid controller pointer");
        return ESP_ERR_INVALID_ARG;
    }

    radiator_t *current = controller->radiator_list;
    radiator_t *prev = NULL;
    bool found = false;

    while (current)
    {
        if (current->radiator_id == radiator_id)
        {
            found = true;
            break;
        }
        prev = current;
        current = current->next;
    }

    if (!found)
    {
        ESP_LOGE(TAG, "Radiator 0x%016llX not found", radiator_id);
        return ESP_ERR_NOT_FOUND;
    }

    if (prev)
    {
        prev->next = current->next;
    }
    else
    {
        controller->radiator_list = current->next;
    }

    ESP_LOGI(TAG, "Removing radiator 0x%016llX", radiator_id);

    free(current);
    controller->radiator_count--;

    esp_err_t save_err = save_radiators_to_nvs(controller);

    if (save_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save radiators after removal: 0x%x", save_err);
        return save_err;
    }

    ESP_LOGI(TAG, "Radiator 0x%016llX successfully removed", radiator_id);
    return ESP_OK;
}

esp_err_t save_radiators_to_nvs(radiator_manager_t *manager)
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

    size_t required_size = sizeof(uint8_t); // radiator_count
    radiator_t *current = manager->radiator_list;

    while (current)
    {
        required_size += sizeof(uint8_t);       // radiator_id
        required_size += sizeof(uint8_t);       // name length
        required_size += strlen(current->name); // name
        required_size += sizeof(uint8_t);       // type
        required_size += sizeof(uint16_t);      // output
        required_size += sizeof(uint64_t);      // flow node
        required_size += sizeof(uint16_t);      // flow endpoint
        required_size += sizeof(uint64_t);      // return node
        required_size += sizeof(uint16_t);      // return endpoint

        current = current->next;
    }

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *ptr = buffer;

    *((uint8_t *)ptr) = manager->radiator_count;
    ptr += sizeof(uint8_t);

    current = manager->radiator_list;
    while (current)
    {
        *((uint8_t *)ptr) = current->radiator_id;
        ptr += sizeof(uint8_t);

        *((uint8_t *)ptr) = strlen(current->name);
        ptr += sizeof(uint8_t);

        memcpy(ptr, current->name, strlen(current->name));
        ptr += strlen(current->name);

        *((uint8_t *)ptr) = current->type;
        ptr += sizeof(uint8_t);

        *((uint16_t *)ptr) = current->outputAtDelta50;
        ptr += sizeof(uint16_t);

        *((uint64_t *)ptr) = current->flow_temp_nodeId;
        ptr += sizeof(uint64_t);

        *((uint16_t *)ptr) = current->flow_temp_endpointId;
        ptr += sizeof(uint16_t);

        *((uint64_t *)ptr) = current->return_temp_nodeId;
        ptr += sizeof(uint64_t);

        *((uint16_t *)ptr) = current->return_temp_endpointId;
        ptr += sizeof(uint16_t);

        current = current->next;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY, buffer, required_size);
    free(buffer);

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    return err;
}

esp_err_t radiator_manager_reset_and_reload(radiator_manager_t *manager)
{
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err == ESP_OK)
    {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        load_radiators_from_nvs(manager);
    }

    return err;
}