#pragma once

#include <stdbool.h>
#include <stdint.h>

// A house has one tariff, but every Electrical Energy Tariff endpoint is kept, not only the one
// picked on Edit Home. A subscription only reports a value again when it changes, so a price
// recorded only after the pick could stay blank until the next tariff period.
#define COMMODITY_PRICE_MAX_ENDPOINTS 4

// The latest readings from one endpoint's Commodity Price cluster, as the cluster reports them.
// Every one is optional, so the has_* flags separate "not reported" from "reported as zero".
typedef struct
{
    // 0 marks a free slot.
    uint64_t node_id;
    uint16_t endpoint_id;

    // TariffUnit: 0 is kWh, 1 is kVAh.
    bool has_tariff_unit;
    uint8_t tariff_unit;

    // Currency: the ISO 4217 numeric code, and how many decimal places `price` carries.
    bool has_currency;
    uint16_t currency;
    uint8_t decimal_points;

    // CurrentPrice.Price: per TariffUnit, in the currency's major unit scaled by 10^decimal_points.
    // Can be negative, on tariffs that pay you to use electricity.
    bool has_price;
    int64_t price;
} commodity_price_endpoint_t;

typedef struct
{
    commodity_price_endpoint_t endpoints[COMMODITY_PRICE_MAX_ENDPOINTS];
} commodity_price_manager_t;

// Transient: nothing here is persisted. A subscription's priming report fills it again.
void commodity_price_manager_init(commodity_price_manager_t *manager);

// NULL if nothing has been reported for the endpoint.
commodity_price_endpoint_t *commodity_price_find_endpoint(commodity_price_manager_t *manager, uint64_t node_id, uint16_t endpoint_id);

// Finds the endpoint's slot, taking a free one if it has none. NULL if the table is full.
commodity_price_endpoint_t *commodity_price_get_endpoint(commodity_price_manager_t *manager, uint64_t node_id, uint16_t endpoint_id);

// Drops every reading held for a node, when it is removed or its subscription ends.
void commodity_price_forget_node(commodity_price_manager_t *manager, uint64_t node_id);
