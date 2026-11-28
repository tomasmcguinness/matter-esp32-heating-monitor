#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Heat and electrical energy over a stretch of the home series, split by what the heat pump was
// doing, so a COP can be given for space heating, for hot water and overall.
//
// Every COP built from these is heat energy / electrical energy over the slots covered -- a ratio
// of integrals, never a mean of the instantaneous cop_x100 column, which weights a minute at
// 300 W the same as a minute at 3 kW.
//
// A slot is hot water if dhw_running is 1, otherwise heating if heating_running is 1, otherwise
// neither -- the same precedence the charts' mode bar uses. `overall` takes every slot, so standby
// and anything else recorded in neither mode lowers the overall figure alone. A day recorded
// before heating_running existed carries no mode and lands in `overall` only.

#ifdef __cplusplus
extern "C" {
#endif

// Watt-seconds, as stream_file()'s energy sums are: power in whole watts times the interval.
typedef struct {
    int64_t heat_ws;
    int64_t elec_ws;
} energy_pair_t;

typedef struct {
    energy_pair_t overall;
    energy_pair_t heating;
    energy_pair_t hot_water;
} history_totals_t;

// Adds one rec_home_t slot. A power field holding the sentinel is skipped on its own, so a slot
// with heat but no electricity still counts its heat -- as the day's energy totals always have.
void history_totals_add(history_totals_t *totals, const uint8_t *rec, uint16_t interval_s);

void history_totals_accumulate(history_totals_t *into, const history_totals_t *from);

// What defrosting used over a stretch of the home series (TASK-41). Separate from
// history_totals_t because that is the cop-totals cache record, whose layout is fixed.
//
// Defrost is a flag of its own, never a mode: a defrost slot is also in `overall` and in whichever
// mode it happened in, so this energy is part of those totals, not in addition to them. Heat is
// the heat meter's net figure while defrosting, normally negative -- the cycle runs in reverse.
//
// `known_slots` counts slots that recorded the flag at all. While it is 0 the defrost figures are
// unknown -- a day from before defrost was recorded, or one converted to the wider record -- not
// zero, and callers report them as null.
//
// `events` counts defrosts: a slot flagged as defrosting after one that was not. `in_defrost` is
// the flag of the last known slot, so a defrost spanning several slots is one event; slots with
// the sentinel leave it alone. Within a day only: each day's count starts afresh, so a defrost
// running across midnight is counted on both days.
//
// This is also the cop-totals cache's defrost record, so its layout is fixed at 32 bytes.
typedef struct {
    uint32_t      known_slots;
    uint32_t      defrost_slots;
    uint32_t      seconds;     // defrost_slots times the interval
    uint16_t      events;
    uint8_t       in_defrost;
    uint8_t       reserved;
    energy_pair_t energy;
} history_defrost_t;

// Adds one rec_home_t slot. As history_totals_add, a power field holding the sentinel is skipped
// on its own.
void history_defrost_add(history_defrost_t *defrost, const uint8_t *rec, uint16_t interval_s);

// Adds one day's (or stretch's) defrost figures to a sum. `in_defrost` is not carried over.
void history_defrost_accumulate(history_defrost_t *into, const history_defrost_t *from);

// Totals for one whole home day file, and what defrosting used in it when `defrost` is not NULL.
// False if it cannot be opened or is not a home series this firmware can read, in which case both
// are left zeroed.
bool history_totals_scan_file(const char *path, history_totals_t *out, history_defrost_t *defrost);

// The per-day cache, /sdcard/cop-totals.
//
// A season cannot be re-read on every request -- a day is 484 KB at the 5 s default -- so each
// completed day is scanned once and its totals appended here, 96 bytes a day. It is a cache of
// derived values and nothing else: every figure in it comes from the day files, so deleting it
// loses nothing and it refills -- from the next restart or midnight if it had already caught up. HISTORY_TOTALS_VERSION is bumped when the arithmetic above
// changes, which discards the file the same way.
//
// It is not named home-*: the dates listing picks out home days by that prefix.

// Summarises at most one completed day that the cache does not hold yet, oldest first. Called
// from the logger's flush task after each flush, so a card full of days fills in at one a minute
// rather than stalling the sampler behind hours of reads. Does nothing once caught up.
void history_totals_backfill_one(void);

// Sums every cached day into `sum`, and their defrost figures into `defrost`. `defrost_days`
// gets how many of those days recorded the defrost flag at all, since days from before it was
// recorded add nothing and are not zero. `first_date` gets the earliest (YYYY-MM-DD, at least 11
// bytes) and is left empty when there are none. Returns the number of days.
uint32_t history_totals_sum_cached(history_totals_t *sum, history_defrost_t *defrost, uint32_t *defrost_days,
                                   char *first_date, size_t first_date_len);

#ifdef __cplusplus
}
#endif
