#include "mcp_series.h"

#include <string.h>

// Field positions are rec_home_t's, in history_format.h.
//
// Nothing is listed that the monitor does not record. There is no target flow temperature, and
// the operating mode is two flags -- heating and hot water -- with no defrost state.
const mcp_series_t MCP_SERIES[] = {
    {"heat_output_w", "Heat output", "W", 0, 1, 1,
     "From the heat meter. Can be negative, e.g. during defrost."},
    {"electrical_input_w", "Electrical input", "W", 1, 1, 1, NULL},
    {"flow_temp_c", "Flow temperature", "degC", 2, 100, 2, "Measured at the heat meter."},
    {"return_temp_c", "Return temperature", "degC", 3, 100, 2, NULL},
    {"outdoor_temp_c", "Outdoor temperature", "degC", 5, 100, 2, NULL},
    {"indoor_temp_c", "Average indoor temperature", "degC", 6, 100, 2,
     "Mean of the rooms that have a sensor."},
    {"flow_rate_lph", "Flow rate", "l/h", 4, 1, 1, NULL},
    {"cop_instantaneous", "Instantaneous COP", "ratio", 7, 100, 2,
     "Heat / electrical power at one instant; null below 50 W electrical. For a COP over a period "
     "use summarise_cycles, which divides energies."},
    {"heating_running", "Space heating running", "0/1", 11, 1, 2,
     "The heat pump's heating thermostat reports heat demand. Null on days recorded before this "
     "was logged. A mean over an interval is the fraction of it spent heating."},
    {"dhw_running", "Hot water running", "0/1", 8, 1, 2,
     "The hot water tank's thermostat reports heat demand. Null on days recorded before this was "
     "logged."},
    {"voltage_v", "Supply voltage", "V", 9, 10, 1, NULL},
    {"current_a", "Supply current", "A", 10, 100, 2, NULL},
};

const size_t MCP_SERIES_COUNT = sizeof(MCP_SERIES) / sizeof(MCP_SERIES[0]);

const mcp_series_t *mcp_series_find(const char *id)
{
    for (size_t i = 0; i < MCP_SERIES_COUNT; i++) {
        if (strcmp(MCP_SERIES[i].id, id) == 0) {
            return &MCP_SERIES[i];
        }
    }
    return NULL;
}

static uint32_t ceil_div(uint32_t a, uint32_t b)
{
    return a / b + (a % b != 0);
}

mcp_plan_t mcp_plan_interval(uint32_t range_s, uint32_t requested_s, uint32_t native_s,
                             uint32_t *interval_s)
{
    if (range_s == 0) {
        return MCP_PLAN_EMPTY_RANGE;
    }

    if (requested_s != 0) {
        if (ceil_div(range_s, requested_s) > MCP_HARD_MAX_POINTS) {
            *interval_s = ceil_div(range_s, MCP_HARD_MAX_POINTS);
            return MCP_PLAN_TOO_MANY_POINTS;
        }
        *interval_s = requested_s;
        return MCP_PLAN_OK;
    }

    if (native_s == 0) {
        native_s = 1;
    }

    uint32_t fit = ceil_div(range_s, MCP_AUTO_MAX_POINTS);
    uint32_t chosen = ceil_div(fit, native_s) * native_s;

    *interval_s = chosen < native_s ? native_s : chosen;
    return MCP_PLAN_OK;
}
