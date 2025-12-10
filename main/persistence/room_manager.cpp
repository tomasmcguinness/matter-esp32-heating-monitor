#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "node_manager.h"

#define NVS_NAMESPACE "matter_nodes"
#define NVS_KEY "node_list"

static const char *TAG = "node_manager";

void node_manager_init(matter_controller_t *controller)
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

    memset(controller, 0, sizeof(matter_controller_t));

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

matter_node_t *find_node(matter_controller_t *controller, uint64_t node_id)
{
    matter_node_t *current = controller->node_list;

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

endpoint_entry_t *find_endpoint(matter_controller_t *controller, matter_node_t *node, uint16_t endpoint_id)
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

esp_err_t remove_node(matter_controller_t *controller, uint64_t node_id)
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

    // for (uint16_t i = 0; i < current->server_clusters_count; i++)
    // {
    //     if (current->server_clusters[i].attributes)
    //     {
    //         free(current->server_clusters[i].attributes);
    //     }
    // }
    // if (current->server_clusters)
    // {
    //     free(current->server_clusters);
    // }

    // for (uint16_t i = 0; i < current->client_clusters_count; i++)
    // {
    //     if (current->client_clusters[i].attributes)
    //     {
    //         free(current->client_clusters[i].attributes);
    //     }
    // }
    // if (current->client_clusters)
    // {
    //     free(current->client_clusters);
    // }

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

matter_node_t *add_node(matter_controller_t *controller, uint64_t node_id)
{
    matter_node_t *new_node = (matter_node_t *)malloc(sizeof(matter_node_t));

    if (!new_node)
    {
        return NULL;
    }

    memset(new_node, 0, sizeof(matter_node_t));

    new_node->node_id = node_id;
    new_node->next = controller->node_list;

    controller->node_list = new_node;
    controller->node_count++;

    return new_node;
}

endpoint_entry_t *add_endpoint(matter_node_t *node, uint16_t endpoint_id)
{
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

uint32_t add_device_type(matter_node_t *node, uint16_t endpoint_id, uint32_t device_type_id)
{
    endpoint_entry_t *endpoint = find_endpoint(NULL, node, endpoint_id);

    if (endpoint == NULL)
    {
        ESP_LOGE(TAG, "Failed to find endpoint %lu", endpoint_id);
        return 0;
    }

    uint32_t *new_device_type_ids = (uint32_t *)realloc(endpoint->device_type_ids, (endpoint->device_type_count + 1) * sizeof(uint32_t));

    endpoint->device_type_ids = new_device_type_ids;
    endpoint->device_type_ids[endpoint->device_type_count] = device_type_id;
    endpoint->device_type_count++;

    ESP_LOGI(TAG, "Added Device Type ID %u to Endpoint %u. There are now %u", device_type_id, endpoint_id, endpoint->device_type_count);

    return device_type_id;
}

void matter_controller_free(matter_controller_t *controller)
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

esp_err_t load_nodes_from_nvs(matter_controller_t *controller)
{
    if (!controller)
    {
        return ESP_ERR_INVALID_ARG;
    }

    matter_controller_free(controller);

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

        ESP_LOGI(TAG, "Loaded node 0x%016llX from NVS", node->node_id);

        // endpoints
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

esp_err_t save_nodes_to_nvs(matter_controller_t *controller)
{
    if (!controller)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t required_size = sizeof(uint16_t); // node_count
    matter_node_t *current = controller->node_list;

    while (current)
    {
        required_size += sizeof(uint64_t);

        // Make space for the endpoints
        required_size += sizeof(uint16_t); // endpoints_count

        for (uint16_t e = 0; e < current->endpoints_count; e++)
        {
            required_size += sizeof(uint16_t); // endpoint_id
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

    *((uint16_t *)ptr) = controller->node_count;
    ptr += sizeof(uint16_t);

    current = controller->node_list;
    while (current)
    {
        *((uint64_t *)ptr) = current->node_id;
        ptr += sizeof(uint64_t);

        // Save endpoints
        *((uint16_t *)ptr) = current->endpoints_count;
        ptr += sizeof(uint16_t);

        for (uint16_t e = 0; e < current->endpoints_count; e++)
        {
            endpoint_entry_t *ep = &current->endpoints[e];
            *((uint16_t *)ptr) = ep->endpoint_id;
            ptr += sizeof(uint16_t);

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