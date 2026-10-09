#include <stdint.h>
#include <stdbool.h>

#pragma once

// Matter device type identifiers that make up a heat pump node.
#define DEVICE_TYPE_HEAT_PUMP 777
#define DEVICE_TYPE_THERMOSTAT 769
#define DEVICE_TYPE_WATER_HEATER 1295

// Endpoints we keep readings for, across every heat pump node. The Kronoterm adapter uses three:
// the heat pump itself, one heating loop and the hot water tank.
#define HEAT_PUMP_MAX_ENDPOINTS 8

// Water Heater Mode's SupportedModes, as far as we keep it. The spec allows a label 64 characters.
#define HEAT_PUMP_MAX_MODES 8
#define HEAT_PUMP_MODE_LABEL_LENGTH 64

// One entry of SupportedModes: the value CurrentMode takes, and what the device calls it.
typedef struct
{
    uint8_t mode;
    char label[HEAT_PUMP_MODE_LABEL_LENGTH + 1];
} heat_pump_mode_t;

// The latest readings from one endpoint of a heat pump node: its electrical power, or the
// Thermostat and Water Heater Management attributes of a heating loop or hot water tank. Values
// are held as the clusters report them. Every one is optional, so the has_* flags separate
// "not reported yet" from "reported as zero".
//
// Temperature Sensor endpoints are not here -- their readings are already on the node manager's
// endpoint_entry_t::measured_value.
typedef struct
{
    // 0 marks a free slot.
    uint64_t node_id;
    uint16_t endpoint_id;

    // ElectricalPowerMeasurement.ActivePower, mW.
    bool has_active_power;
    int64_t active_power_mw;

    // Thermostat.LocalTemperature and OccupiedHeatingSetpoint, 0.01 degC.
    bool has_local_temperature;
    int16_t local_temperature;

    bool has_heating_setpoint;
    int16_t heating_setpoint;

    // Thermostat.SystemMode (SystemModeEnum) and ThermostatRunningState (RelayStateBitmap).
    bool has_system_mode;
    uint8_t system_mode;

    bool has_running_state;
    uint16_t running_state;

    // WaterHeaterManagement.HeatDemand (WaterHeaterHeatSourceBitmap) and BoostState (BoostStateEnum).
    bool has_heat_demand;
    uint8_t heat_demand;

    bool has_boost_state;
    uint8_t boost_state;

    // WaterHeaterMode.CurrentMode and SupportedModes. The mode values are the device's own, so
    // CurrentMode only means something alongside the label SupportedModes gives it.
    bool has_water_heater_mode;
    uint8_t water_heater_mode;

    uint8_t supported_mode_count;
    heat_pump_mode_t supported_modes[HEAT_PUMP_MAX_MODES];
} heat_pump_endpoint_t;

typedef struct
{
    heat_pump_endpoint_t endpoints[HEAT_PUMP_MAX_ENDPOINTS];
} heat_pump_manager_t;

// Transient: nothing here is persisted. A subscription's priming report fills it again.
void heat_pump_manager_init(heat_pump_manager_t *manager);

// NULL if nothing has been reported for the endpoint.
heat_pump_endpoint_t *heat_pump_find_endpoint(heat_pump_manager_t *manager, uint64_t node_id, uint16_t endpoint_id);

// Finds the endpoint's slot, taking a free one if it has none. NULL if the table is full.
heat_pump_endpoint_t *heat_pump_get_endpoint(heat_pump_manager_t *manager, uint64_t node_id, uint16_t endpoint_id);

// The device's label for the endpoint's current water heater mode. NULL if the mode has not been
// reported, or SupportedModes does not list it.
const char *heat_pump_water_heater_mode_label(const heat_pump_endpoint_t *endpoint);

// Drops every reading held for a node, when it is removed or its subscription ends.
void heat_pump_forget_node(heat_pump_manager_t *manager, uint64_t node_id);
