#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_matter_controller_subscribe_command.h>

#include "node_manager.h"
#include "app_main.h"

#include <set>

#define NVS_NAMESPACE "matter_nodes"
#define NVS_KEY "node_list"

using namespace chip::app::Clusters;
using namespace esp_matter::controller;

static const char *TAG = "node_manager";

void node_manager_init(node_manager_t *controller)
{
    if (controller == NULL)
    {
        ESP_LOGE(TAG, "Controller pointer is NULL!");
        return;
    }

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    memset(controller, 0, sizeof(node_manager_t));

    esp_err_t load_err = load_nodes_from_nvs(controller);

    if (load_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded %d nodes from NVS", controller->node_count);
    }
    else
    {
        ESP_LOGW(TAG, "No saved nodes found in NVS (err: 0x%x)", load_err);
    }
}

uint64_t get_next_node_id(node_manager_t *manager)
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
        ESP_LOGI(TAG, "No current_id found in NVS, starting from seed");
        next_id = 10000;
    }
    else if (err == ESP_OK)
    {
        // We got the current value. Bump it.
        ESP_LOGI(TAG, "Found current_id in NVS: %llu", current_id);
        next_id = current_id + 1;
    }
    else
    {
        ESP_LOGI(TAG, "Failed to find current_id in NVS: %d", err);
        nvs_close(nvs_handle);
        return 0;
    }

    nvs_set_u64(nvs_handle, "current_id", next_id);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);  

    ESP_LOGI(TAG, "Next node ID is %llu", next_id);   

    return next_id;
}

void subscribe_all_temperature_measurements(node_manager_t *manager)
{
    ESP_LOGI(TAG, "Subscribing to all temperature measurements...");

    matter_node_t *node = manager->node_list;

    while (node)
    {
        auto *args = new std::tuple<uint64_t>(node->node_id);

        chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg)
                                                      {
            auto *args = reinterpret_cast<std::tuple<uint64_t> *>(arg);

            ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
            attr_paths.Alloc(1);

            if (!attr_paths.Get())
            {
                ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
                return;
            }

            attr_paths[0] = AttributePathParams(TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id);

            ScopedMemoryBufferWithSize<EventPathParams> event_paths;
            event_paths.Alloc(0);

            auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(std::get<0>(*args),
                std::move(attr_paths),
                std::move(event_paths),
                0,
                60,
                false, // <--- Keep Subscriptions
                attribute_data_cb,
                nullptr,
                node_subscription_established_cb,
                node_subscription_terminated_cb,
                node_subscribe_failed_cb,
                false); 

            if (!cmd)
            {
                ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
            }
            else
            {
                esp_err_t err = cmd->send_command();
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to send subscribe command: %s", esp_err_to_name(err));
                }
            } },
                                                      reinterpret_cast<intptr_t>(args));

        node = node->next;
    }
}

matter_node_t *find_node(node_manager_t *manager, uint64_t node_id)
{
    matter_node_t *current = manager->node_list;

    while (current != NULL)
    {
        if (current->node_id == node_id)
        {
            return current;
        }

        current = current->next;
    }

    return NULL;
}

endpoint_entry_t *find_endpoint(matter_node_t *node, uint16_t endpoint_id)
{
    for (uint16_t i = 0; i < node->endpoints_count; i++)
    {
        if (node->endpoints[i].endpoint_id == endpoint_id)
        {
            return &node->endpoints[i];
        }
    }

    return NULL;
}

esp_err_t remove_node(node_manager_t *controller, uint64_t node_id)
{
    if (!controller)
    {
        ESP_LOGE(TAG, "Invalid controller pointer");
        return ESP_ERR_INVALID_ARG;
    }

    matter_node_t *current = controller->node_list;
    matter_node_t *prev = NULL;
    bool found = false;

    while (current)
    {
        if (current->node_id == node_id)
        {
            found = true;
            break;
        }
        prev = current;
        current = current->next;
    }

    if (!found)
    {
        ESP_LOGE(TAG, "Node 0x%016llX not found", node_id);
        return ESP_ERR_NOT_FOUND;
    }

    if (prev)
    {
        prev->next = current->next;
    }
    else
    {
        controller->node_list = current->next;
    }

    ESP_LOGI(TAG, "Removing node 0x%016llX", node_id);

    if (current->endpoints)
    {
        // TODO Free device_type_ids inside each endpoint
        free(current->endpoints);
    }

    free(current);
    controller->node_count--;

    esp_err_t save_err = save_nodes_to_nvs(controller);

    if (save_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save nodes after removal: 0x%x", save_err);
        return save_err;
    }

    ESP_LOGI(TAG, "Node 0x%016llX successfully removed", node_id);
    return ESP_OK;
}

matter_node_t *add_node(node_manager_t *controller, uint64_t node_id)
{
    matter_node_t *new_node = (matter_node_t *)malloc(sizeof(matter_node_t));

    if (!new_node)
    {
        return NULL;
    }

    memset(new_node, 0, sizeof(matter_node_t));

    new_node->node_id = node_id;
    new_node->vendor_name = "not-specified";
    new_node->product_name = "not-specified";
    new_node->name = NULL;
    new_node->label = NULL;

    new_node->next = controller->node_list;

    controller->node_list = new_node;
    controller->node_count++;

    return new_node;
}

endpoint_entry_t *add_endpoint(matter_node_t *node, uint16_t endpoint_id)
{
    // Check if endpoint already exists
    endpoint_entry_t *existing_endpoint = find_endpoint(node, endpoint_id);
    
    if (existing_endpoint)
    {
        ESP_LOGW(TAG, "Endpoint %u already exists in node 0x%016llX", endpoint_id, node->node_id);
        return existing_endpoint;
    }

    endpoint_entry_t *new_endpoints = (endpoint_entry_t *)realloc(node->endpoints, (node->endpoints_count + 1) * sizeof(endpoint_entry_t));

    if (!new_endpoints)
    {
        return NULL;
    }

    node->endpoints = new_endpoints;
    endpoint_entry_t *ep = &node->endpoints[node->endpoints_count];
    memset(ep, 0, sizeof(endpoint_entry_t));
    ep->endpoint_id = endpoint_id;

    node->endpoints_count++;

    return ep;
}

esp_err_t set_endpoint_name(matter_node_t *node, uint16_t endpoint_id, char *name)
{
    endpoint_entry_t *endpoint = find_endpoint(node, endpoint_id);

    if (endpoint == NULL)
    {
        ESP_LOGE(TAG, "Failed to find endpoint %lu", endpoint_id);
        return ESP_FAIL;
    }

    endpoint->name = name;

    return ESP_OK;
}

esp_err_t set_endpoint_power_source(matter_node_t *node, uint16_t endpoint_id, uint8_t power_source)
{
    endpoint_entry_t *endpoint = find_endpoint(node, endpoint_id);

    if (endpoint == NULL)
    {
        ESP_LOGE(TAG, "Failed to find endpoint %lu", endpoint_id);
        return ESP_FAIL;
    }

    endpoint->power_source = power_source;

    return ESP_OK;
}

esp_err_t set_node_name(matter_node_t *node, char *name)
{
    node->name = name;
    return ESP_OK;
}

esp_err_t set_node_label(matter_node_t *node, char *label)
{
    node->label = label;

    if (!node->name)
    {
        ESP_LOGI(TAG, "Node has no name, so using label");
        set_node_name(node, label);
    }

    return ESP_OK;
}

esp_err_t set_node_power_source(matter_node_t *node, uint8_t power_source)
{
    node->power_source = power_source;
    return ESP_OK;
}

esp_err_t set_endpoint_measured_value(node_manager_t *manager, uint64_t node_id, uint16_t endpoint_id, uint16_t measured_value)
{
    matter_node_t *node = find_node(manager, node_id);

    if (node)
    {
        endpoint_entry_t *endpoint = find_endpoint(node, endpoint_id);

        if (endpoint)
        {
            endpoint->measured_value = measured_value;
        }
    }

    return ESP_OK;
}

esp_err_t mark_node_has_subscription(node_manager_t *manager, uint64_t node_id, uint32_t subscription_id)
{
    matter_node_t *node = find_node(manager, node_id);

    if (node)
    {
        node->has_subscription = true;
        node->subscription_id = subscription_id;
    }

    return ESP_OK;
}

esp_err_t mark_node_has_no_subscription(node_manager_t *manager, uint64_t node_id, uint32_t subscription_id)
{
    matter_node_t *node = find_node(manager, node_id);

    if (node && node->subscription_id == subscription_id)
    {
        node->has_subscription = false;
        node->subscription_id = 0;
    }

    return ESP_OK;
}

esp_err_t add_device_type(matter_node_t *node, uint16_t endpoint_id, uint32_t device_type_id)
{
    endpoint_entry_t *endpoint = find_endpoint(node, endpoint_id);

    if (endpoint == NULL)
    {
        ESP_LOGE(TAG, "Failed to find endpoint %lu", endpoint_id);
        return ESP_FAIL;
    }

    uint32_t *new_device_type_ids = (uint32_t *)realloc(endpoint->device_type_ids, (endpoint->device_type_count + 1) * sizeof(uint32_t));

    endpoint->device_type_ids = new_device_type_ids;
    endpoint->device_type_ids[endpoint->device_type_count] = device_type_id;
    endpoint->device_type_count++;

    ESP_LOGI(TAG, "Added Device Type ID %u to Endpoint %u. There are now %u", device_type_id, endpoint_id, endpoint->device_type_count);

    return ESP_OK;
}

esp_err_t clear_node_details(node_manager_t *manager, uint64_t node_id)
{
    matter_node_t *node = find_node(manager, node_id);

    if (node)
    {
        ESP_LOGI(TAG, "Clearing node %llu of existing endpoint details", node_id);
        node->endpoints_count = 0;

        // TODO Free existing memory

        return ESP_OK;
    }

    return ESP_FAIL;
}

void node_manager_free(node_manager_t *controller)
{
    matter_node_t *current = controller->node_list;

    while (current != NULL)
    {
        matter_node_t *next = current->next;

        if (current->endpoints)
        {
            free(current->endpoints);
        }

        free(current);

        current = next;
    }

    controller->node_list = NULL;
    controller->node_count = 0;
}

esp_err_t load_nodes_from_nvs(node_manager_t *controller)
{
    if (!controller)
    {
        return ESP_ERR_INVALID_ARG;
    }

    node_manager_free(controller);

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
    uint16_t node_count = *((uint16_t *)ptr);
    ptr += sizeof(uint16_t);

    for (uint16_t i = 0; i < node_count; i++)
    {
        matter_node_t *node = (matter_node_t *)calloc(1, sizeof(matter_node_t));
        if (!node)
        {
            free(buffer);
            return ESP_ERR_NO_MEM;
        }

        node->node_id = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        ESP_LOGI(TAG, "Processing node 0x%016llX from NVS", node->node_id);

        // Vendor
        node->vendor_name_length = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        node->vendor_name = (char *)calloc(node->vendor_name_length + 1, sizeof(char));

        memcpy(node->vendor_name, ptr, node->vendor_name_length);
        ptr += node->vendor_name_length;

        // Product
        node->product_name_length = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        node->product_name = (char *)calloc(node->product_name_length + 1, sizeof(char));

        memcpy(node->product_name, ptr, node->product_name_length);
        ptr += node->product_name_length;

        // Name
        node->name_length = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        node->name = (char *)calloc(node->name_length + 1, sizeof(char));

        memcpy(node->name, ptr, node->name_length);
        ptr += node->name_length;

        // Label
        node->label_length = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        node->label = (char *)calloc(node->label_length + 1, sizeof(char));

        memcpy(node->label, ptr, node->label_length);
        ptr += node->label_length;

        // Power Source
        node->power_source = *((uint8_t *)ptr);
        ptr += sizeof(uint8_t);

        // Endpoints
        node->endpoints_count = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        ESP_LOGI(TAG, "Node 0x%016llX has %u endpoints", node->node_id, node->endpoints_count);

        if (node->endpoints_count > 0)
        {
            node->endpoints = (endpoint_entry_t *)calloc(node->endpoints_count, sizeof(endpoint_entry_t));
            for (uint16_t e = 0; e < node->endpoints_count; e++)
            {
                endpoint_entry_t *ep = &node->endpoints[e];
                ep->endpoint_id = *((uint16_t *)ptr);
                ptr += sizeof(uint16_t);

                ep->name_length = *((uint8_t *)ptr);
                ptr += sizeof(uint8_t);

                ep->name = (char *)calloc(ep->name_length + 1, sizeof(char));

                memcpy(ep->name, ptr, ep->name_length);
                ptr += ep->name_length;

                ep->power_source = *((uint8_t *)ptr);
                ptr += sizeof(uint8_t);

                ep->device_type_count = *((uint16_t *)ptr);
                ptr += sizeof(uint16_t);

                ESP_LOGI(TAG, "Node 0x%016llX - endpoint %u has %u device types", node->node_id, ep->endpoint_id, ep->device_type_count);

                ep->device_type_ids = (uint32_t *)calloc(ep->device_type_count, sizeof(uint32_t));
                for (uint16_t dt = 0; dt < ep->device_type_count; dt++)
                {
                    ep->device_type_ids[dt] = *((uint32_t *)ptr);
                    ptr += sizeof(uint32_t);
                }
            }
        }

        node->next = controller->node_list;
        controller->node_list = node;
    }

    controller->node_count = node_count;
    free(buffer);

    return ESP_OK;
}

esp_err_t save_nodes_to_nvs(node_manager_t *manager)
{
    ESP_LOGI(TAG, "Saving nodes...");

    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t required_size = sizeof(uint16_t); // node_count
    matter_node_t *current = manager->node_list;

    while (current)
    {
        required_size += sizeof(uint64_t); // node_id

        // Basic information.
        required_size += sizeof(uint16_t);
        required_size += current->vendor_name ? strlen(current->vendor_name) : 0;
        required_size += sizeof(uint16_t);
        required_size += current->product_name ? strlen(current->product_name) : 0;

        // Name
        required_size += sizeof(uint8_t);
        required_size += current->name ? strlen(current->name) : 0;

        // Save Label
        required_size += sizeof(uint8_t);
        required_size += current->label ? strlen(current->label) : 0;

        // Power Source
        required_size += sizeof(uint8_t);

        // Endpoint Count
        required_size += sizeof(uint16_t);

        for (uint16_t e = 0; e < current->endpoints_count; e++)
        {
            required_size += sizeof(uint16_t); // endpoint_id

            required_size += sizeof(uint8_t);
            required_size += current->endpoints[e].name ? strlen(current->endpoints[e].name) : 0;

            required_size += sizeof(uint8_t); // power_source

            required_size += sizeof(uint16_t); // device_type_count

            for (uint16_t dt = 0; dt < current->endpoints[e].device_type_count; dt++)
            {
                required_size += sizeof(uint32_t); // device_type_id
            }
        }

        current = current->next;
    }

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *ptr = buffer;

    *((uint16_t *)ptr) = manager->node_count;
    ptr += sizeof(uint16_t);

    current = manager->node_list;
    while (current)
    {
        *((uint64_t *)ptr) = current->node_id;
        ptr += sizeof(uint64_t);

        // Vendor
        if (current->vendor_name)
        {
            *((uint16_t *)ptr) = strlen(current->vendor_name);
            ptr += sizeof(uint16_t);

            memcpy(ptr, current->vendor_name, strlen(current->vendor_name));
            ptr += strlen(current->vendor_name);
        }
        else
        {
            *((uint16_t *)ptr) = 0;
            ptr += sizeof(uint16_t);
        }

        // Product
        if (current->product_name)
        {
            *((uint16_t *)ptr) = strlen(current->product_name);
            ptr += sizeof(uint16_t);

            memcpy(ptr, current->product_name, strlen(current->product_name));
            ptr += strlen(current->product_name);
        }
        else
        {
            *((uint16_t *)ptr) = 0;
            ptr += sizeof(uint16_t);
        }

        // Name
        if (current->name)
        {
            *((uint8_t *)ptr) = strlen(current->name);
            ptr += sizeof(uint8_t);

            memcpy(ptr, current->name, strlen(current->name));
            ptr += strlen(current->name);
        }
        else
        {
            *((uint8_t *)ptr) = 0;
            ptr += sizeof(uint8_t);
        }

        // Label
        if (current->label)
        {
            *((uint8_t *)ptr) = strlen(current->label);
            ptr += sizeof(uint8_t);

            memcpy(ptr, current->label, strlen(current->label));
            ptr += strlen(current->label);
        }
        else
        {
            *((uint8_t *)ptr) = 0;
            ptr += sizeof(uint8_t);
        }

        // Save power source
        *((uint8_t *)ptr) = current->power_source;
        ptr += sizeof(uint8_t);

        // Save endpoints
        *((uint16_t *)ptr) = current->endpoints_count;
        ptr += sizeof(uint16_t);

        for (uint16_t e = 0; e < current->endpoints_count; e++)
        {
            endpoint_entry_t *ep = &current->endpoints[e];
            *((uint16_t *)ptr) = ep->endpoint_id;
            ptr += sizeof(uint16_t);

            if (ep->name)
            {
                *((uint8_t *)ptr) = strlen(ep->name);
                ptr += sizeof(uint8_t);

                memcpy(ptr, ep->name, strlen(ep->name));
                ptr += strlen(ep->name);
            }
            else
            {
                *((uint8_t *)ptr) = 0;
                ptr += sizeof(uint8_t);
            }

            // Power Source
            *((uint8_t *)ptr) = ep->power_source;
            ptr += sizeof(uint8_t);

            // Save device types
            *((uint16_t *)ptr) = ep->device_type_count;
            ptr += sizeof(uint16_t);

            for (uint16_t dt = 0; dt < ep->device_type_count; dt++)
            {
                *((uint32_t *)ptr) = ep->device_type_ids[dt];
                ptr += sizeof(uint32_t);
            }
        }

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