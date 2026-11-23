#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "radiator_manager.h"

#define NVS_NAMESPACE "radiators"
#define NVS_KEY "radiator_list"

// UFH manifold calibrations, kept apart from radiator_list so that blob's layout never changes:
// u8 version, u8 count, then per manifold u8 radiator_id, u8 loop_count, u16 ref_flow_lph,
// u32 calibrated_at and loop_count x u16 flow (0.1 l/min), all little-endian.
#define NVS_KEY_UFH_LOOPS "ufh_loops"
#define UFH_LOOPS_BLOB_VERSION 1
#define UFH_LOOPS_ENTRY_HEADER 8

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

uint8_t get_next_radiator_id(radiator_manager_t *manager)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %d", NVS_NAMESPACE, err);
        return 0;
    }

    uint64_t next_id = 0;
    uint64_t current_id = 0;
    err = nvs_get_u64(nvs_handle, "current_id", &current_id);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        // The key doesn't exist.
        next_id = 1;
    }
    else if (err == ESP_OK)
    {
        // We got the current value. Bump it.
        next_id = current_id + 1;
    }
    else
    {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Failed to get current_id from NVS: %d", err);
        return 0;
    }

    nvs_set_u64(nvs_handle, "current_id", next_id);
    nvs_commit(nvs_handle);

    return next_id;
}

// Reads ufh_loops into manifolds already loaded. A missing key is no calibrations; an entry for a
// record that no longer exists or is no longer a manifold, or that is out of range, is dropped.
static void load_ufh_loops_from_nvs(radiator_manager_t *manager);
static esp_err_t save_ufh_loops_to_nvs(nvs_handle_t nvs_handle, radiator_manager_t *manager);

uint32_t ufh_calibrated_flow_lph(const radiator_t *radiator)
{
    uint32_t dlpm = 0;

    for (uint8_t i = 0; i < radiator->ufh_loop_count; i++)
    {
        dlpm += radiator->ufh_loop_flow_dlpm[i];
    }

    // 0.1 l/min is 6 l/h.
    return dlpm * 6;
}

esp_err_t set_ufh_calibration(radiator_manager_t *manager, radiator_t *radiator, uint8_t loop_count, const uint16_t *loop_flow_dlpm, uint16_t ref_flow_lph, uint32_t calibrated_at)
{
    if (!manager || !radiator || !is_ufh_manifold(radiator) || loop_count == 0 || loop_count > UFH_MAX_LOOPS || ref_flow_lph == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < loop_count; i++)
    {
        if (loop_flow_dlpm[i] == 0 || loop_flow_dlpm[i] > UFH_MAX_LOOP_FLOW_DLPM)
        {
            return ESP_ERR_INVALID_ARG;
        }
    }

    memset(radiator->ufh_loop_flow_dlpm, 0, sizeof(radiator->ufh_loop_flow_dlpm));
    memcpy(radiator->ufh_loop_flow_dlpm, loop_flow_dlpm, loop_count * sizeof(uint16_t));
    radiator->ufh_loop_count = loop_count;
    radiator->ufh_ref_flow_lph = ref_flow_lph;
    radiator->ufh_calibrated_at = calibrated_at;

    return save_radiators_to_nvs(manager);
}

static void load_ufh_loops_from_nvs(radiator_manager_t *manager)
{
    nvs_handle_t nvs_handle;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) != ESP_OK)
    {
        return;
    }

    size_t size = 0;
    esp_err_t err = nvs_get_blob(nvs_handle, NVS_KEY_UFH_LOOPS, NULL, &size);

    if (err != ESP_OK || size < 2)
    {
        nvs_close(nvs_handle);
        return;
    }

    uint8_t *buffer = (uint8_t *)malloc(size);

    if (!buffer)
    {
        nvs_close(nvs_handle);
        return;
    }

    err = nvs_get_blob(nvs_handle, NVS_KEY_UFH_LOOPS, buffer, &size);
    nvs_close(nvs_handle);

    if (err != ESP_OK || buffer[0] != UFH_LOOPS_BLOB_VERSION)
    {
        ESP_LOGW(TAG, "Ignoring unreadable UFH loop blob");
        free(buffer);
        return;
    }

    uint8_t count = buffer[1];
    size_t offset = 2;

    for (uint16_t i = 0; i < count; i++)
    {
        if (offset + UFH_LOOPS_ENTRY_HEADER > size)
        {
            break;
        }

        const uint8_t *entry = buffer + offset;
        uint8_t loop_count = entry[1];
        size_t entry_size = UFH_LOOPS_ENTRY_HEADER + (size_t)loop_count * sizeof(uint16_t);

        if (offset + entry_size > size)
        {
            ESP_LOGW(TAG, "UFH loop blob is truncated");
            break;
        }

        offset += entry_size;

        radiator_t *radiator = find_radiator(manager, entry[0]);
        uint16_t ref_flow_lph;
        uint32_t calibrated_at;
        uint16_t flows[UFH_MAX_LOOPS];

        if (!radiator || loop_count == 0 || loop_count > UFH_MAX_LOOPS)
        {
            continue;
        }

        memcpy(&ref_flow_lph, entry + 2, sizeof(ref_flow_lph));
        memcpy(&calibrated_at, entry + 4, sizeof(calibrated_at));
        memcpy(flows, entry + UFH_LOOPS_ENTRY_HEADER, loop_count * sizeof(uint16_t));

        // Straight into the record rather than through set_ufh_calibration(), which would save.
        if (is_ufh_manifold(radiator) && ref_flow_lph > 0)
        {
            memcpy(radiator->ufh_loop_flow_dlpm, flows, loop_count * sizeof(uint16_t));
            radiator->ufh_loop_count = loop_count;
            radiator->ufh_ref_flow_lph = ref_flow_lph;
            radiator->ufh_calibrated_at = calibrated_at;
        }
    }

    free(buffer);
}

static esp_err_t save_ufh_loops_to_nvs(nvs_handle_t nvs_handle, radiator_manager_t *manager)
{
    size_t count = 0;
    size_t size = 2;

    for (radiator_t *radiator = manager->radiator_list; radiator != NULL; radiator = radiator->next)
    {
        if (is_ufh_manifold(radiator) && ufh_is_calibrated(radiator))
        {
            count++;
            size += UFH_LOOPS_ENTRY_HEADER + (size_t)radiator->ufh_loop_count * sizeof(uint16_t);
        }
    }

    if (count > UINT8_MAX)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *buffer = (uint8_t *)malloc(size);

    if (!buffer)
    {
        return ESP_ERR_NO_MEM;
    }

    buffer[0] = UFH_LOOPS_BLOB_VERSION;
    buffer[1] = (uint8_t)count;
    uint8_t *entry = buffer + 2;

    for (radiator_t *radiator = manager->radiator_list; radiator != NULL; radiator = radiator->next)
    {
        if (!is_ufh_manifold(radiator) || !ufh_is_calibrated(radiator))
        {
            continue;
        }

        entry[0] = radiator->radiator_id;
        entry[1] = radiator->ufh_loop_count;
        memcpy(entry + 2, &radiator->ufh_ref_flow_lph, sizeof(uint16_t));
        memcpy(entry + 4, &radiator->ufh_calibrated_at, sizeof(uint32_t));
        memcpy(entry + UFH_LOOPS_ENTRY_HEADER, radiator->ufh_loop_flow_dlpm, radiator->ufh_loop_count * sizeof(uint16_t));
        entry += UFH_LOOPS_ENTRY_HEADER + radiator->ufh_loop_count * sizeof(uint16_t);
    }

    esp_err_t err = nvs_set_blob(nvs_handle, NVS_KEY_UFH_LOOPS, buffer, size);
    free(buffer);

    return err;
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

radiator_t *add_radiator(radiator_manager_t *manager, char *name, char *mqtt_name, uint8_t type, uint16_t output_dt_50, uint64_t flow_temp_node_id, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId)
{
    uint8_t new_radiator_id = get_next_radiator_id(manager);

    if (new_radiator_id == 0)
    {
        ESP_LOGE(TAG, "Failed to get next radiator ID");
        return NULL;
    }

    ESP_LOGI(TAG, "Adding a radiator with ID %d", new_radiator_id);

    radiator_t *new_radiator = (radiator_t *)calloc(1, sizeof(radiator_t));

    if (!new_radiator)
    {
        return NULL;
    }

    memset(new_radiator, 0, sizeof(radiator_t));

    new_radiator->radiator_id = new_radiator_id;
    new_radiator->name = (char *)malloc(strlen(name) + 1);
    strcpy(new_radiator->name, name);

    new_radiator->mqtt_name = (char *)malloc(strlen(mqtt_name) + 1);
    strcpy(new_radiator->mqtt_name, mqtt_name);

    new_radiator->type = type;
    new_radiator->output_dt_50 = output_dt_50;
    new_radiator->flow_temp_node_id = flow_temp_node_id;
    new_radiator->flow_temp_endpoint_id = flowEndpointId;
    new_radiator->return_temp_node_id = returnNodeId;
    new_radiator->return_temp_endpoint_id = returnEndpointId;

    new_radiator->next = manager->radiator_list;

    manager->radiator_list = new_radiator;
    manager->radiator_count++;

    return new_radiator;
}

radiator_t *update_radiator(radiator_manager_t *manager, uint8_t radiator_id, char *name, char *mqtt_name, uint8_t type, uint16_t output_dt_50, uint64_t flow_temp_node_id, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId)
{
    radiator_t *radiator = find_radiator(manager, radiator_id);

    if (radiator)
    {
        free(radiator->name);
        radiator->name = (char *)malloc(strlen(name) + 1);
        strcpy(radiator->name, name);

        free(radiator->mqtt_name);
        radiator->mqtt_name = (char *)malloc(strlen(mqtt_name) + 1);
        strcpy(radiator->mqtt_name, mqtt_name);

        // A calibration belongs to a manifold: it goes if the record stops being one.
        if (type != EMITTER_TYPE_UFH_MANIFOLD)
        {
            radiator->ufh_loop_count = 0;
            radiator->ufh_ref_flow_lph = 0;
            radiator->ufh_calibrated_at = 0;
        }

        radiator->type = type;
        radiator->output_dt_50 = output_dt_50;
        radiator->flow_temp_node_id = flow_temp_node_id;
        radiator->flow_temp_endpoint_id = flowEndpointId;
        radiator->return_temp_node_id = returnNodeId;
        radiator->return_temp_endpoint_id = returnEndpointId;

        return radiator;
    }

    return nullptr;
}

esp_err_t remove_radiator(radiator_manager_t *controller, uint8_t radiator_id, char *mqtt_name)
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

    ESP_LOGI(TAG, "Removing radiator %u", radiator_id);

    strcpy(mqtt_name, current->mqtt_name);

    free(current->name);
    free(current->mqtt_name);
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

void radiator_manager_free(radiator_manager_t *manager)
{
    radiator_t *current = manager->radiator_list;

    while (current != NULL)
    {
        radiator_t *next = current->next;

        free(current->name);
        free(current->mqtt_name);
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
    {
        return err;
    }

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

        // Name
        radiator->name_len = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        radiator->name = (char *)calloc(radiator->name_len + 1, sizeof(char));

        memcpy(radiator->name, ptr, radiator->name_len);
        ptr += radiator->name_len;

        // MQTT Name
        radiator->mqtt_name_len = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        radiator->mqtt_name = (char *)calloc(radiator->mqtt_name_len + 1, sizeof(char));

        memcpy(radiator->mqtt_name, ptr, radiator->mqtt_name_len);
        ptr += radiator->mqtt_name_len;

        radiator->type = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        radiator->output_dt_50 = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        radiator->flow_temp_node_id = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        radiator->flow_temp_endpoint_id = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        radiator->return_temp_node_id = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        radiator->return_temp_endpoint_id = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        ESP_LOGI(TAG, "Loaded radiator 0x%0X from NVS", radiator->radiator_id);

        radiator->next = manager->radiator_list;
        manager->radiator_list = radiator;
    }

    manager->radiator_count = radiator_count;

    free(buffer);

    load_ufh_loops_from_nvs(manager);

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
    {
        return err;
    }

    size_t required_size = sizeof(uint8_t); // radiator_count
    radiator_t *current = manager->radiator_list;

    manager->radiator_count = 0;

    while (current)
    {
        required_size += sizeof(uint8_t);            // radiator_id
        required_size += sizeof(uint8_t);            // name length
        required_size += strlen(current->name);      // name
        required_size += sizeof(uint8_t);            // mqtt name length
        required_size += strlen(current->mqtt_name); // mqtt name
        required_size += sizeof(uint8_t);            // type
        required_size += sizeof(uint16_t);           // output
        required_size += sizeof(uint64_t);           // flow node
        required_size += sizeof(uint16_t);           // flow endpoint
        required_size += sizeof(uint64_t);           // return node
        required_size += sizeof(uint16_t);           // return endpoint

        current = current->next;

        manager->radiator_count++;
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

        *((uint8_t *)ptr) = strlen(current->mqtt_name);
        ptr += sizeof(uint8_t);

        memcpy(ptr, current->mqtt_name, strlen(current->mqtt_name));
        ptr += strlen(current->mqtt_name);

        *((uint8_t *)ptr) = current->type;
        ptr += sizeof(uint8_t);

        *((uint16_t *)ptr) = current->output_dt_50;
        ptr += sizeof(uint16_t);

        *((uint64_t *)ptr) = current->flow_temp_node_id;
        ptr += sizeof(uint64_t);

        *((uint16_t *)ptr) = current->flow_temp_endpoint_id;
        ptr += sizeof(uint16_t);

        *((uint64_t *)ptr) = current->return_temp_node_id;
        ptr += sizeof(uint64_t);

        *((uint16_t *)ptr) = current->return_temp_endpoint_id;
        ptr += sizeof(uint16_t);

        current = current->next;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY, buffer, required_size);
    free(buffer);

    if (err == ESP_OK)
    {
        err = save_ufh_loops_to_nvs(nvs_handle, manager);
    }

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