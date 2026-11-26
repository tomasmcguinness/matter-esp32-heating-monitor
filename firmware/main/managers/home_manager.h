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

    // The endpoint carrying the Heat Pump device type. Its readings, and those of the other
    // endpoints on the same node, are held by heat_pump_manager.
    uint64_t heat_pump_node_id;
    uint16_t heat_pump_endpoint_id;

    // The Electrical Energy Tariff endpoint whose Commodity Price the running cost uses. Its
    // readings are held by commodity_price_manager.
    uint64_t commodity_price_node_id;
    uint16_t commodity_price_endpoint_id;

    // The Humidity Sensor endpoint whose RelativeHumidityMeasurement is the outdoor humidity.
    // Appended to the end of the NVS blob, after the commodity price.
    uint64_t outdoor_humidity_node_id;
    uint16_t outdoor_humidity_endpoint_id;

    // How often the SD card history logger samples, in seconds. 0 means "never saved",
    // which history_logger_init() reads as "use the default".
    uint16_t logging_interval_s;

    bool has_outdoor_temperature;
    int16_t outdoor_temperature;

    // Relative humidity outdoors, 0.01 %, as RelativeHumidityMeasurement reports it (0-10000).
    // Transient like every reading, and absent once the sensor's subscription is lost.
    bool has_outdoor_humidity;
    uint16_t outdoor_humidity;

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

    // True while the heat pump is defrosting its outdoor coil. Independent of the two above: a
    // defrost happens during heating or hot water and does not change which is running. From the
    // StateValue of the picked heat pump node's Boolean State endpoint labelled "Defrost" -- see
    // update_heat_pump_running(). Absent for a heat pump with no such endpoint, and until it reports.
    // Transient like the rest of the readings: not in the NVS blob.
    bool has_defrosting;
    bool defrosting;

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
    // Whether that total is complete: every radiator that is in a room has an output. One short of a
    // probe reading has none, and the total is then too low to compare with the heat meter.
    bool has_total_radiator_output = false;

    // Underfloor heating, derived by update_ufh_outputs(). Transient. The total is the sum of every
    // manifold's output; the unassigned part is what no room has a share of. Absent while any
    // manifold has no output (see update_ufh_outputs()), and when there are no manifolds.
    uint8_t ufh_manifold_count = 0;
    bool has_ufh_output = false;
    uint16_t total_ufh_output = 0;
    uint16_t ufh_unassigned_output = 0;

} home_manager_t;

// Whether the heat pump is defrosting: its defrost flag, or the heat meter reading negative power,
// which is the same thing seen from the meter (and the only sign of it from a heat pump with no
// defrost endpoint). A defrost happens inside a heating or hot water run and leaves that run's flag
// on, but while it lasts the heat pump is taking heat back from the water: radiators and UFH give no
// output, and the heat meter's flow is pushed up for the defrost rather than set by the emitters.
static inline bool home_is_defrosting(const home_manager_t *h)
{
    return (h->has_defrosting && h->defrosting) || (h->has_heat_meter_power && h->heat_meter_power_mw < 0);
}

void home_manager_init(home_manager_t *manager);

esp_err_t load_home_from_nvs(home_manager_t *manager);
esp_err_t save_home_to_nvs(home_manager_t *manager);