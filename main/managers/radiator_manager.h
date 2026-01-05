#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

typedef struct radiator
{
    uint8_t radiator_id;
    uint8_t name_len;
    char *name;
    uint8_t type;
    uint16_t output_dt_50;

    uint64_t flow_temp_node_id;
    uint16_t flow_temp_endpoint_id;
    uint64_t return_temp_node_id;
    uint16_t return_temp_endpoint_id;

    uint8_t room_id;

    // Transient data
    int16_t flow_temperature;
    int16_t return_temperature;
    uint16_t heat_output;

    struct radiator *next;

} radiator;

typedef radiator radiator_t;

typedef struct
{
    radiator_t *radiator_list;
    uint8_t radiator_count;
} radiator_manager_t;

void radiator_manager_init(radiator_manager_t *manager);

radiator_t *find_radiator(radiator_manager_t *manager, uint8_t radiator_id);

radiator_t *add_radiator(radiator_manager_t *manager, char *name, uint8_t type, uint16_t output, uint64_t flowNodeId, uint16_t flowEndpointId, uint64_t returnNodeId, uint16_t returnEndpointId);
esp_err_t remove_radiator(radiator_manager_t *manager, uint8_t radiator_id);

esp_err_t update_radiator(radiator_manager_t *manager, uint8_t radiator_id, uint16_t output);

esp_err_t load_radiators_from_nvs(radiator_manager_t *manager);
esp_err_t save_radiators_to_nvs(radiator_manager_t *manager);

esp_err_t radiator_manager_reset_and_reload(radiator_manager_t *manager);