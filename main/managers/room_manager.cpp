#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "room_manager.h"

#define NVS_NAMESPACE "rooms"
#define NVS_KEY "room_list"

static const char *TAG = "room_manager";

void room_manager_init(room_manager_t *manager)
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

    memset(manager, 0, sizeof(room_manager_t));

    esp_err_t load_err = load_rooms_from_nvs(manager);

    if (load_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded %d rooms from NVS", manager->room_count);
    }
    else
    {
        ESP_LOGW(TAG, "No saved rooms found in NVS (err: 0x%x)", load_err);
    }
}

room_t *add_room(room_manager_t *manager, char *name, uint64_t room_temperature_nodeId, uint16_t room_temperature_endpointId)
{
    uint8_t new_room_id = manager->room_count + 1;

    room_t *new_room = (room_t *)calloc(1, sizeof(room_t));
    if (!new_room)
    {
        return NULL;
    }

    memset(new_room, 0, sizeof(room_t));

    new_room->room_id = new_room_id;
    new_room->name_len = strlen(name);
    new_room->room_temperature_nodeId = room_temperature_nodeId;
    new_room->room_temperature_endpointId = room_temperature_endpointId;
    new_room->name = name;

    new_room->next = manager->room_list;

    manager->room_list = new_room;
    manager->room_count++;

    return new_room;
}

void room_manager_free(room_manager_t *manager)
{
    room_t *current = manager->room_list;

    while (current != NULL)
    {
        room_t *next = current->next;

        free(current);

        current = next;
    }

    manager->room_list = NULL;
    manager->room_count = 0;
}

esp_err_t load_rooms_from_nvs(room_manager_t *manager)
{
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    room_manager_free(manager);

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
    uint8_t room_count = *((uint8_t *)ptr);
    ptr += sizeof(uint8_t);

    for (uint16_t i = 0; i < room_count; i++)
    {
        room_t *room = (room_t *)calloc(1, sizeof(room_t));
        if (!room)
        {
            free(buffer);
            return ESP_ERR_NO_MEM;
        }

        room->room_id = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        room->room_temperature_nodeId = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        room->room_temperature_endpointId = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        room->name_len = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        room->name = (char *)calloc(1, room->name_len + 1);

        memcpy(room->name, ptr, room->name_len);
        room->name[room->name_len] = 0x00;
        ptr += room->name_len;

        ESP_LOGI(TAG, "Loaded room %u from NVS", room->room_id);

        room->next = manager->room_list;
        manager->room_list = room;
    }

    manager->room_count = room_count;
    free(buffer);

    return ESP_OK;
}

esp_err_t remove_room(room_manager_t *manager, uint8_t room_id)
{
    if (!manager)
    {
        ESP_LOGE(TAG, "Invalid manager pointer");
        return ESP_ERR_INVALID_ARG;
    }

    room_t *current = manager->room_list;
    room_t *prev = NULL;
    bool found = false;

    while (current)
    {
        if (current->room_id == room_id)
        {
            found = true;
            break;
        }
        prev = current;
        current = current->next;
    }

    if (!found)
    {
        ESP_LOGE(TAG, "Room 0x%016llX not found", room_id);
        return ESP_ERR_NOT_FOUND;
    }

    if (prev)
    {
        prev->next = current->next;
    }
    else
    {
        manager->room_list = current->next;
    }

    ESP_LOGI(TAG, "Removing room 0x%016llX", room_id);

    free(current);
    manager->room_count--;

    esp_err_t save_err = save_rooms_to_nvs(manager);

    if (save_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save rooms after removal: 0x%x", save_err);
        return save_err;
    }

    ESP_LOGI(TAG, "Room 0x%016llX successfully removed", room_id);
    return ESP_OK;
}

esp_err_t save_rooms_to_nvs(room_manager_t *manager)
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

    size_t required_size = sizeof(uint8_t); // room_count
    room_t *current = manager->room_list;

    while (current)
    {
        required_size += sizeof(uint8_t); // room_id
        required_size += sizeof(uint64_t); // room_temperature_nodeId
        required_size += sizeof(uint16_t); // room_temperature_endpointId
        required_size += sizeof(uint8_t); // name length
        required_size += strlen(current->name); // name

        current = current->next;
    }

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *ptr = buffer;

    *((uint8_t *)ptr) = manager->room_count;
    ptr += sizeof(uint8_t);

    current = manager->room_list;
    while (current)
    {
        *((uint8_t *)ptr) = current->room_id;
        ptr += sizeof(uint8_t);

        *((uint64_t *)ptr) = current->room_temperature_nodeId;
        ptr += sizeof(uint64_t);

        *((uint16_t *)ptr) = current->room_temperature_endpointId;
        ptr += sizeof(uint16_t);

        *((uint8_t *)ptr) = strlen(current->name);
        ptr += sizeof(uint8_t);

        memcpy(ptr, current->name, strlen(current->name));
        ptr += strlen(current->name);

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

esp_err_t room_manager_reset_and_reload(room_manager_t *manager) {

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    nvs_erase_all(nvs_handle);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    load_rooms_from_nvs(manager);

    return ESP_OK;
}