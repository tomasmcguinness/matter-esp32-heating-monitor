#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sd_card.h"

// Path construction for the history files, shared by the logger and the reader so the
// two cannot drift on naming.
//
//   /sdcard/home-YYYY-MM-DD        KIND_HOME      (a singleton; inst is ignored)
//   /sdcard/room-<id>/YYYY-MM-DD   KIND_ROOM
//   /sdcard/rad-<id>/YYYY-MM-DD    KIND_RADIATOR
//
// Rooms and radiators get a directory each rather than a flat name, because the listing
// endpoint opendir()s the mount point and walks every entry: flat, a dozen radiators over a
// year would put thousands of long-filename entries in one directory for every date lookup to
// scan past. Per-instance directories keep each listing to that instance's own days.
//
// Home stays flat, where it already is, so that history written before rooms and radiators
// existed is still found.

#define HISTORY_PATH_MAX 96

#ifdef __cplusplus
extern "C" {
#endif

// Copies a caller-supplied token into out only if it is filesystem-safe: [A-Za-z0-9_-],
// length 1..32. Blocks '/', '.' and anything else that could escape the mount point.
// Ported from matter-esp32-home-energy-manager (node_power_logger.cpp:58).
bool history_sanitize_token(const char *tok, char *out, size_t out_len);

// The directory a series lives in. False for KIND_HOME, which has none. The logger uses this
// to mkdir before the first write of a day; the reader, to list an instance's dates.
bool history_dir_for(uint8_t kind, uint8_t inst, char *out, size_t out_len);

// Builds the path for a series on the local day containing base_ts. Returns false if the
// kind is unknown or the path would not fit. inst is the room or radiator id, ignored for
// KIND_HOME.
bool history_path_for(uint8_t kind, uint8_t inst, uint32_t base_ts, char *out, size_t out_len);

// Builds the path from an already-sanitised date string, for the read endpoints.
bool history_path_for_date(uint8_t kind, uint8_t inst, const char *date, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
