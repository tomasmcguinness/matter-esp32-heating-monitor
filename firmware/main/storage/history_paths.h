#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sd_card.h"

// Path construction for the history files, shared by the logger and the reader so the
// two cannot drift on naming.
//
//   /sdcard/sensor-<nodeId>-<endpointId>-YYYY-MM-DD
//   /sdcard/home-YYYY-MM-DD

#define HISTORY_PATH_MAX 96

#ifdef __cplusplus
extern "C" {
#endif

// Copies a caller-supplied token into out only if it is filesystem-safe: [A-Za-z0-9_-],
// length 1..32. Blocks '/', '.' and anything else that could escape the mount point.
// Ported from matter-esp32-home-energy-manager (node_power_logger.cpp:58).
bool history_sanitize_token(const char *tok, char *out, size_t out_len);

// Builds the path for a series on the local day containing base_ts. Returns false if the
// kind is unknown or the path would not fit.
bool history_path_for(uint8_t kind, uint64_t node_id, uint16_t endpoint_id,
                      uint32_t base_ts, char *out, size_t out_len);

// Builds the path from an already-sanitised date string, for the read endpoints.
bool history_path_for_date(uint8_t kind, uint64_t node_id, uint16_t endpoint_id,
                           const char *date, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
