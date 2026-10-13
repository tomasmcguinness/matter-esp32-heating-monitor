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

// Field positions are rec_room_t's and rec_radiator_t's. Both store 0.01 degC.
static const struct {
    const char  *prefix;
    const char  *suffix;
    mcp_source_t source;
    uint8_t      field;
} INSTANCE_SERIES[] = {
    {"room_", "_temp_c", MCP_SOURCE_ROOM, 0},
    {"radiator_", "_flow_temp_c", MCP_SOURCE_RADIATOR, 0},
    {"radiator_", "_return_temp_c", MCP_SOURCE_RADIATOR, 1},
};

bool mcp_series_resolve(const char *id, mcp_series_ref_t *out)
{
    const mcp_series_t *home = mcp_series_find(id);

    if (home) {
        *out = (mcp_series_ref_t){MCP_SOURCE_HOME, 0, home->field, home->divisor, home->decimals};
        return true;
    }

    for (size_t i = 0; i < sizeof(INSTANCE_SERIES) / sizeof(INSTANCE_SERIES[0]); i++) {
        size_t prefix_len = strlen(INSTANCE_SERIES[i].prefix);

        if (strncmp(id, INSTANCE_SERIES[i].prefix, prefix_len) != 0) {
            continue;
        }

        // The id between the prefix and the suffix: one to three digits, no leading zero, 255 at
        // most. Parsed by hand so that "room_+1_temp_c" and "room_01_temp_c" are not second
        // names for room 1.
        const char *digits = id + prefix_len;
        unsigned    inst   = 0;
        size_t      n      = 0;

        while (digits[n] >= '0' && digits[n] <= '9' && n < 3) {
            inst = inst * 10 + (unsigned)(digits[n] - '0');
            n++;
        }

        if (n == 0 || (n > 1 && digits[0] == '0') || inst > 255 ||
            strcmp(digits + n, INSTANCE_SERIES[i].suffix) != 0) {
            continue;
        }

        *out = (mcp_series_ref_t){INSTANCE_SERIES[i].source, (uint8_t)inst, INSTANCE_SERIES[i].field, 100, 2};
        return true;
    }

    return false;
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
