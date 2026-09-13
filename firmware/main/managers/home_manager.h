#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

typedef struct {
    uint64_t outdoor_temp_node_id;
    uint16_t outdoor_temp_endpoint_id;

    uint64_t electrical_meter_node_id;
    uint16_t electrical_meter_endpoint_id;

    uint64_t heat_meter_node_id;
    uint16_t heat_meter_endpoint_id;

    // How often the SD card history logger samples, in seconds. 0 means "never saved",
    // which history_logger_init() reads as "use the default".
    uint16_t logging_interval_s;

    bool has_outdoor_temperature;
    int16_t outdoor_temperature;

    // ElectricalPowerMeasurement readings, in the units the cluster reports them: mV, mA and mW.
    // All three are nullable, so the has_* flags separate "not reported yet" from "reported as zero".
    bool has_electrical_voltage;
    int64_t electrical_voltage_mv;

    bool has_electrical_current;
    int64_t electrical_current_ma;

    bool has_electrical_power;
    int64_t electrical_power_mw;

    bool has_heat_meter_flow;
    uint16_t heat_meter_flow;

    bool has_heat_meter_flow_temperature;
    int32_t heat_meter_flow_temperature;

    bool has_heat_meter_return_temperature;
    int32_t heat_meter_return_temperature;

    bool has_heat_meter_power;
    int64_t heat_meter_power_mw;

    // Indoor temperature representative of the home as a whole. No sensor is bound to this
    // yet, so it stays absent and the history logs it as "no reading".
    bool has_internal_temperature;
    int16_t internal_temperature;

    // True while the heat source is producing domestic hot water rather than space heating.
    // No source is bound to this yet.
    bool has_dhw_running;
    bool dhw_running;

    // Instantaneous coefficient of performance, 0.01 units, derived by update_home().
    bool has_cop;
    int16_t cop_x100;

    // Transient
    uint16_t total_predicted_heat_loss_per_degree = 0;
    uint16_t total_measured_heat_loss_per_degree = 0;
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