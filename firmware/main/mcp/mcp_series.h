#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The series the MCP server offers, and the arithmetic that decides how many points a request
// gets. Plain C with no ESP-IDF in it, so test_host/ can check the caps on a desktop.

#ifdef __cplusplus
extern "C" {
#endif

// get_series returns this many points per series when the caller names no interval, and refuses
// to return more than the second however it is asked.
#define MCP_AUTO_MAX_POINTS 1000
#define MCP_HARD_MAX_POINTS 5000

// One column of rec_home_t, in the units a caller wants it in.
typedef struct {
    const char *id;
    const char *name;
    const char *unit;
    uint8_t     field;    // position in rec_home_t
    uint16_t    divisor;  // stored value / divisor = value in `unit`
    uint8_t     decimals; // printed with this many
    const char *note;     // NULL for none
} mcp_series_t;

extern const mcp_series_t MCP_SERIES[];
extern const size_t       MCP_SERIES_COUNT;

// NULL if there is no series with that id.
const mcp_series_t *mcp_series_find(const char *id);

typedef enum {
    MCP_PLAN_OK,
    MCP_PLAN_EMPTY_RANGE,     // end is not after start
    MCP_PLAN_TOO_MANY_POINTS, // *interval_s is set to the smallest interval that would fit
} mcp_plan_t;

// The interval a request is answered at. `requested_s` is 0 when the caller named none, in which
// case the smallest whole multiple of `native_s` that keeps the response to MCP_AUTO_MAX_POINTS is
// chosen. A requested interval that would give more than MCP_HARD_MAX_POINTS is refused.
mcp_plan_t mcp_plan_interval(uint32_t range_s, uint32_t requested_s, uint32_t native_s,
                             uint32_t *interval_s);

#ifdef __cplusplus
}
#endif
