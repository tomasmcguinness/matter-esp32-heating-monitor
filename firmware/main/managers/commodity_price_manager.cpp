#include <string.h>
#include <esp_log.h>

#include "commodity_price_manager.h"

static const char *TAG = "commodity_price_manager";

void commodity_price_manager_init(commodity_price_manager_t *manager)
{
    if (manager == NULL)
    {
        ESP_LOGE(TAG, "Manager pointer is NULL!");
        return;
    }

    memset(manager, 0, sizeof(commodity_price_manager_t));
}

commodity_price_endpoint_t *commodity_price_find_endpoint(commodity_price_manager_t *manager, uint64_t node_id, uint16_t endpoint_id)
{
    if (node_id == 0)
    {
        return NULL;
    }

    for (size_t i = 0; i < COMMODITY_PRICE_MAX_ENDPOINTS; i++)
    {
        commodity_price_endpoint_t *endpoint = &manager->endpoints[i];

        if (endpoint->node_id == node_id && endpoint->endpoint_id == endpoint_id)
        {
            return endpoint;
        }
    }

    return NULL;
}

commodity_price_endpoint_t *commodity_price_get_endpoint(commodity_price_manager_t *manager, uint64_t node_id, uint16_t endpoint_id)
{
    if (node_id == 0)
    {
        return NULL;
    }

    commodity_price_endpoint_t *endpoint = commodity_price_find_endpoint(manager, node_id, endpoint_id);

    if (endpoint)
    {
        return endpoint;
    }

    for (size_t i = 0; i < COMMODITY_PRICE_MAX_ENDPOINTS; i++)
    {
        endpoint = &manager->endpoints[i];

        if (endpoint->node_id == 0)
        {
            memset(endpoint, 0, sizeof(commodity_price_endpoint_t));
            endpoint->node_id = node_id;
            endpoint->endpoint_id = endpoint_id;
            return endpoint;
        }
    }

    ESP_LOGW(TAG, "No room for node 0x%016llX endpoint %u", node_id, endpoint_id);

    return NULL;
}

void commodity_price_forget_node(commodity_price_manager_t *manager, uint64_t node_id)
{
    for (size_t i = 0; i < COMMODITY_PRICE_MAX_ENDPOINTS; i++)
    {
        commodity_price_endpoint_t *endpoint = &manager->endpoints[i];

        if (endpoint->node_id == node_id)
        {
            memset(endpoint, 0, sizeof(commodity_price_endpoint_t));
        }
    }
}
