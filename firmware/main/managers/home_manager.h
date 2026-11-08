#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

// The most rooms the underfloor heating can be split between.
#define UFH_MAX_SHARES 16

// The underfloor heating's output below 0.5 K across its flow and return is not believed: the
// circuit is not circulating, and the residual is radiator modelling error, not floor heat.
#define UFH_MIN_DELTA_T_C100 50

// One room's share of the underfloor heating's output, in whole percent.
typedef struct {
    uint8_t room_id;
    uint8_t pct;
} ufh_share_t;

typedef struct {
    uint64_t outdoor_temp_node_id;
    uint16_t outdoor_temp_endpoint_id;

    uint64_t electrical_meter_node_id;
    uint16_t electrical_meter_endpoint_id;

    uint64_t heat_meter_node_id;
    uint16_t heat_meter_endpoint_id;

    // The endpoint carrying the Heat Pump device type. Its readings, and those of the other
    // endpoints on the same node, are held by heat_pump_manager.
    uint64_t heat_pump_node_id;
    uint16_t heat_pump_endpoint_id;

    // The Electrical Energy Tariff endpoint whose Commodity Price the running cost uses. Its
    // readings are held by commodity_price_manager.
    uint64_t commodity_price_node_id;
    uint16_t commodity_price_endpoint_id;

    // The one underfloor heating circuit: its flow and return probes, and how its output is split
    // between rooms. Its output is not measured -- see update_ufh_output() -- so the probes only
    // gate it and are recorded. A node id of 0 is "not picked".
    uint64_t ufh_flow_node_id;
    uint16_t ufh_flow_endpoint_id;
    uint64_t ufh_return_node_id;
    uint16_t ufh_return_endpoint_id;

    uint8_t ufh_share_count;
    ufh_share_t ufh_shares[UFH_MAX_SHARES];

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

    // Indoor temperature representative of the home as a whole, 0.01 degC: the mean of the
    // rooms' temperatures, derived by update_home(). Absent while no room has a reading.
    bool has_average_internal_temperature;
    int16_t average_internal_temperature;

    // True while the heat pump is producing domestic hot water, and while it is space heating.
    // Both come from the picked heat pump node's ThermostatRunningState -- see
    // update_heat_pump_running() in app_main.cpp. Absent until an endpoint of that kind reports.
    bool has_dhw_running;
    bool dhw_running;

    bool has_heating_running;
    bool heating_running;

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

    // The underfloor heating probes' latest readings, 0.01 degC.
    bool has_ufh_flow_temperature;
    int16_t ufh_flow_temperature;
    bool has_ufh_return_temperature;
    int16_t ufh_return_temperature;

    // The underfloor heating's output in W, derived by update_ufh_output(): the heat meter's
    // output less the radiators'. Absent while it cannot be worked out.
    bool has_ufh_output;
    uint16_t ufh_output_w;

} home_manager_t;

// The share of the underfloor heating's output room_id takes, in whole percent. 0 if none.
uint8_t home_ufh_share_for_room(const home_manager_t *manager, uint8_t room_id);

// Drops room_id's share, for when the room is removed. True if it had one.
bool home_ufh_forget_room(home_manager_t *manager, uint8_t room_id);

void home_manager_init(home_manager_t *manager);

esp_err_t load_home_from_nvs(home_manager_t *manager);
esp_err_t save_home_to_nvs(home_manager_t *manager);