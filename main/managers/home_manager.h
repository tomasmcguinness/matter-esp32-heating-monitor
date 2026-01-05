#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

typedef struct {
    uint64_t outdoor_temp_node_id;
    uint16_t outdoor_temp_endpoint_id;

    uint64_t heat_source_flow_temp_node_id;
    uint16_t heat_source_flow_endpoint_id;

    uint64_t heat_source_return_temp_node_id;
    uint16_t heat_source_return_endpoint_id;

    int16_t outdoor_temperature;
    
} home_manager_t;

void home_manager_init(home_manager_t *manager);

esp_err_t load_home_from_nvs(home_manager_t *manager);
esp_err_t save_home_to_nvs(home_manager_t *manager);