#pragma once

#include <stddef.h>
#include <stdint.h>

// The rooms and radiators as configured now: what list_rooms reports alongside the recorded
// series, so a caller can tell which id is the kitchen and what a radiator is rated at.
//
// This is the MCP server's one view of the managers, and it is read-only and by value. The
// server itself is plain C and is also built on a desktop by test_host/, where these two
// functions are stand-ins; on the board they are in mcp_directory.cpp.
//
// Both must be called from the HTTP task. The manager lists are linked lists whose nodes the HTTP
// handlers add and free, so that is the one task that can walk them without a lock.

#define MCP_NAME_MAX           48
#define MCP_ROOM_RADIATORS_MAX 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t id;
    char    name[MCP_NAME_MAX];
    int16_t target_temp_c100;
    uint8_t heat_loss_w_per_c; // predicted, as entered
    uint8_t radiator_count;
    uint8_t radiators[MCP_ROOM_RADIATORS_MAX];
} mcp_room_info_t;

typedef struct {
    uint8_t  id;
    char     name[MCP_NAME_MAX];
    uint16_t rated_output_w; // at a mean-water-to-air difference of 50 degC
} mcp_radiator_info_t;

// Each fills up to `max` entries and returns how many it wrote.
size_t mcp_directory_rooms(mcp_room_info_t *out, size_t max);
size_t mcp_directory_radiators(mcp_radiator_info_t *out, size_t max);

#ifdef __cplusplus
}
#endif
