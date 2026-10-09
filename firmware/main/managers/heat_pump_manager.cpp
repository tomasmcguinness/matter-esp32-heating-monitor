#include <string.h>
#include <esp_log.h>

#include "heat_pump_manager.h"

static const char *TAG = "heat_pump_manager";

void heat_pump_manager_init(heat_pump_manager_t *manager)
{
    if (manager == NULL)
    {
        ESP_LOGE(TAG, "Manager pointer is NULL!");
        return;
    }

    memset(manager, 0, sizeof(heat_pump_manager_t));
}

heat_pump_endpoint_t *heat_pump_find_endpoint(heat_pump_manager_t *manager, uint64_t node_id, uint16_t endpoint_id)
{
    if (node_id == 0)
    {
        return NULL;
    }

    for (size_t i = 0; i < HEAT_PUMP_MAX_ENDPOINTS; i++)
    {
        heat_pump_endpoint_t *endpoint = &manager->endpoints[i];

        if (endpoint->node_id == node_id && endpoint->endpoint_id == endpoint_id)
        {
            return endpoint;
        }
    }

    return NULL;
}

heat_pump_endpoint_t *heat_pump_get_endpoint(heat_pump_manager_t *manager, uint64_t node_id, uint16_t endpoint_id)
{
    if (node_id == 0)
    {
        return NULL;
    }

    heat_pump_endpoint_t *endpoint = heat_pump_find_endpoint(manager, node_id, endpoint_id);

    if (endpoint)
    {
        return endpoint;
    }

    for (size_t i = 0; i < HEAT_PUMP_MAX_ENDPOINTS; i++)
    {
        endpoint = &manager->endpoints[i];

        if (endpoint->node_id == 0)
        {
            memset(endpoint, 0, sizeof(heat_pump_endpoint_t));
            endpoint->node_id = node_id;
            endpoint->endpoint_id = endpoint_id;
            return endpoint;
        }
    }

    ESP_LOGW(TAG, "No room for node 0x%016llX endpoint %u; reading dropped", node_id, endpoint_id);

    return NULL;
}

const char *heat_pump_water_heater_mode_label(const heat_pump_endpoint_t *endpoint)
{
    if (endpoint == NULL || !endpoint->has_water_heater_mode)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < endpoint->supported_mode_count; i++)
    {
        if (endpoint->supported_modes[i].mode == endpoint->water_heater_mode)
        {
            return endpoint->supported_modes[i].label;
        }
    }

    return NULL;
}

void heat_pump_forget_node(heat_pump_manager_t *manager, uint64_t node_id)
{
    if (node_id == 0)
    {
        return;
    }

    for (size_t i = 0; i < HEAT_PUMP_MAX_ENDPOINTS; i++)
    {
        if (manager->endpoints[i].node_id == node_id)
        {
            memset(&manager->endpoints[i], 0, sizeof(heat_pump_endpoint_t));
        }
    }
}
