#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

typedef struct {
    uint64_t outdoor_temp_node_id;
    uint16_t outdoor_temp_endpoint_id;

    uint64_t heat_source_flow_temp_node_id;
    uint16_t heat_source_flow_temp_endpoint_id;

    uint64_t heat_source_return_temp_node_id;
    uint16_t heat_source_return_temp_endpoint_id;

    uint64_t heat_source_flow_rate_node_id;
    uint16_t heat_source_flow_rate_endpoint_id;

    int16_t outdoor_temperature;
    int16_t heat_source_flow_temperature;
    int16_t heat_source_return_temperature;
    uint16_t heat_source_flow_rate;
    uint16_t heat_source_output;

    // Transient
    uint16_t total_predicted_heat_loss_at_target_temperature = 0;
    uint16_t total_predicted_heat_loss_at_current_temperature = 0;
    uint16_t total_measured_heat_loss_at_target_temperature = 0;
    uint16_t total_measured_heat_loss_at_current_temperature = 0;

    uint8_t radiator_count = 0;
    uint16_t total_radiator_output = 0;
    
} home_manager_t;

void home_manager_init(home_manager_t *manager);

esp_err_t load_home_from_nvs(home_manager_t *manager);
esp_err_t save_home_to_nvs(home_manager_t *manager);