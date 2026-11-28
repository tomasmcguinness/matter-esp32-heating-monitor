#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Time at target, per room per day: how long each room spent under, on and over its target
// temperature, and its mean distance from the target. This feeds the Rooms page's "Last 7 days"
// and "Avg vs target" columns and its grid, and the daily chart on each room's page.
//
// A slot is **on target** within COMFORT_BAND_C100 of the target, either way. That is the Heat
// tab's band, so a room marked on target there is on target here. Only slots with both a
// temperature and a target count; the rest are in none of the three.
//
// Every figure comes twice. `all` takes every slot. `heating` takes only the slots in which the
// heat pump was space heating (`heating_running` 1 and `dhw_running` not 1 in the home series, the
// mode strip's precedence). Overnight setback or the heating being off makes a room read cold in
// `all` when it is fine whenever the heating is on, and `heating` leaves that out.
//
// **The target is recorded per slot** in rec_room_t's target_temp_c100 from 2026-11-28 on. A slot
// without one, from a day recorded before that, is judged against the room's target now, and the
// day is flagged COMFORT_TARGET_ESTIMATED, since the target may have been different then.

#ifdef __cplusplus
extern "C" {
#endif

#define COMFORT_BAND_C100 20

// Flags on a day's record.
#define COMFORT_TARGET_ESTIMATED 0x01 // some slots used the room's current target
#define COMFORT_HEATING_UNKNOWN  0x02 // the home series could not be read, so `heating` is empty

// Seconds under, on and over the target, and the sum of (temperature - target) in 0.01 degC times
// seconds over the same slots. The mean distance is offset_cs / (under_s + on_s + over_s).
typedef struct {
    uint32_t under_s;
    uint32_t on_s;
    uint32_t over_s;
    uint32_t reserved;
    int64_t  offset_cs;
} comfort_split_t;

// One room's day. Also the record of the /sdcard/comfort-days cache, so its layout is fixed.
typedef struct {
    char            date[11]; // YYYY-MM-DD, NUL-terminated
    uint8_t         room_id;
    uint8_t         flags;
    uint8_t         reserved[3];
    comfort_split_t all;
    comfort_split_t heating;
} comfort_record_t;

// Adds one slot. `target_c100` is the slot's target, already resolved to the room's current one
// where the slot had none. Either value missing adds nothing.
void comfort_add(comfort_split_t *split, bool has_temp, int16_t temp_c100, bool has_target, int16_t target_c100,
                 uint16_t interval_s);

// One room's figures for one day, from its day file and the home day file. False when the room
// has no file for the day, or it cannot be read. `out` is filled in either way, date and id
// included.
bool history_comfort_scan_day(uint8_t room_id, const char *date, comfort_record_t *out);

// The per-day cache, /sdcard/comfort-days: one record per room for every completed day, plus a
// record with room_id COMFORT_DAY_DONE for each day summarised, so a day with no rooms still moves
// the cursor on. Like cop-totals (history_totals.h), every figure in it comes from the day files,
// so deleting it loses nothing: it refills one day a minute.
#define COMFORT_DAY_DONE 0xFF

// Summarises at most one completed day the cache does not hold yet. Called from the logger's
// flush task. Also refreshes today's running figures every few calls.
void history_comfort_backfill_one(void);

// Calls `emit` for every cached record dated on or after `from_date`, oldest first, then for
// today's running figures. Returns false when the cache cannot be read; today is still emitted.
typedef void (*comfort_emit_fn)(const comfort_record_t *rec, void *ctx);
bool history_comfort_each(const char *from_date, comfort_emit_fn emit, void *ctx);

// Supplied by history_logger.cpp: the room's target now, in 0.01 degC, from the logger's
// snapshot. False for a room that no longer exists.
bool history_logger_room_target(uint8_t room_id, int16_t *target_c100);

#ifdef __cplusplus
}
#endif
