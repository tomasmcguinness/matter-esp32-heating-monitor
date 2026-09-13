#pragma once

#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

// On-disk format for the SD card history.
//
// History records QUANTITIES, not sensors. One file per local day:
//
//   /sdcard/home-YYYY-MM-DD
//
// holding every value the home tracks -- heat, electrical, weather -- sourced from
// home_manager rather than from whichever device happens to supply them. Replacing a heat
// meter therefore leaves the recorded flow temperature continuous, where a per-sensor
// archive keyed on nodeId would have orphaned it and started again.
//
// Readings are sampled on a fixed cadence, so records carry no timestamp: slot i is
// base_ts + i * interval_s, which makes a lookup
//
//   offset = sizeof(history_header_t) + slot * record_size
//
// -- O(1) arithmetic, no search and no scan. Keeping interval_s and record_size in the
// header means changing the sampling interval does not invalidate files already
// written, and a reader can skip a file whose version it does not know.
//
// Slot arithmetic is pure unix arithmetic against base_ts, so a 23- or 25-hour local
// day at a DST change simply has fewer or more slots. Nothing may assume 17280.

#ifdef __cplusplus
extern "C" {
#endif

#define HISTORY_MAGIC   0x31534D48u // 'HMS1' little-endian
#define HISTORY_VERSION 2

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  sensor_kind;
    uint16_t interval_s;  // the interval THIS file was written at
    uint32_t base_ts;     // unix time of local midnight for this day
    uint16_t record_size;
    uint16_t reserved;
} history_header_t;

#ifdef __cplusplus
#define HIST_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define HIST_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

HIST_STATIC_ASSERT(sizeof(history_header_t) == 16, "history_header_t must stay 16 bytes");

// Only one series is written today. The kind is still carried in the header and threaded
// through the path and record helpers, so adding a second series later (one at a different
// cadence, say) costs a new enum value rather than a format change.
typedef enum {
    KIND_HOME = 5,
} sensor_kind_t;

// "No reading in this slot". Both sit outside any physical range -- INT16_MIN as
// 0.01 degC is -327.68 degC. Zero cannot be used: 0 W is a real electrical reading, a
// distinction the UI already makes (ElectricalPower.tsx renders 0 but dashes on null).
#define HIST_NULL_I16 INT16_MIN
#define HIST_NULL_U16 UINT16_MAX

// Power is stored in whole watts as int16, which caps at +/-32.7 kW. That is well above
// a domestic heat pump but it IS a cap; widen to int32 here if this ever drives
// something industrial.

// The record is deliberately wider than the quantities in use, and the spare fields are
// part of the design rather than an oversight.
//
// record_size is what makes offset = header + slot * record_size correct, so widening the
// record is a breaking change: the reader rejects a file whose record_size disagrees with
// the kind. Reserving fields up front means a quantity added later fills a spare slot
// instead, record_size never moves, and days already on the card stay readable -- they
// simply carry the sentinel in that column, which the reader already renders as null.
// history_api.c names the columns in its response, so the UI picks up a new one from the
// device rather than from a recompile.
//
// Field order is load-bearing in two places: history_api.c integrates fields 0 and 1 for
// energyWh/COP, and History.tsx charts by position. Append to the reserved tail; never
// reorder.
typedef struct __attribute__((packed)) {
    int16_t  heat_power_w;        // Heat Meter power,        mW -> W
    int16_t  elec_power_w;        // ElectricalPower.ActivePower, mW -> W
    int16_t  flow_temp_c100;      // 0.01 degC
    int16_t  return_temp_c100;    // 0.01 degC
    uint16_t flow_lph;            // Heat Meter flow, l/h
    int16_t  outdoor_temp_c100;   // 0.01 degC
    int16_t  internal_temp_c100;  // 0.01 degC -- no source bound yet
    int16_t  cop_x100;            // derived by update_home(), 0.01
    int16_t  dhw_running;         // 0 or 1 -- no source bound yet
    uint16_t elec_voltage_dv;     // Voltage,       mV -> 0.1 V
    uint16_t elec_current_ca;     // ActiveCurrent, mA -> 0.01 A
    int16_t  reserved0;
    int16_t  reserved1;
    int16_t  reserved2;
} rec_home_t;

HIST_STATIC_ASSERT(sizeof(rec_home_t) == 28, "rec_home_t must stay 28 bytes");

// Largest record, for fixed-size buffers in the writer and reader.
#define HISTORY_MAX_RECORD_SIZE 28

static inline size_t history_record_size(uint8_t kind)
{
    switch (kind) {
    case KIND_HOME: return sizeof(rec_home_t);
    default:        return 0;
    }
}

// Every field of every record above is exactly 16 bits wide, so writer and reader can
// both walk a record as an array of 16-bit fields. What differs is signedness, and with
// it which sentinel means "absent" -- hence the mask rather than a single fill value.
static inline size_t history_field_count(uint8_t kind)
{
    return history_record_size(kind) / 2;
}

// Bit i set => field i of this kind is unsigned, so its sentinel is HIST_NULL_U16.
//
// The mask is a uint16_t, one bit per field, which caps any record at 16 fields / 32
// bytes. Widen this type before adding a 17th field.
static inline uint16_t history_unsigned_mask(uint8_t kind)
{
    switch (kind) {
    // flow_lph (4), elec_voltage_dv (9), elec_current_ca (10)
    case KIND_HOME: return 0x0610;
    default:        return 0x0000;
    }
}

// Fills a record with "no reading" for every field. Used to pad slots the logger missed,
// so that offset = header + slot * record_size stays true across an outage.
static inline void history_fill_sentinel(uint8_t kind, void *out)
{
    uint16_t mask = history_unsigned_mask(kind);
    size_t   n    = history_field_count(kind);

    for (size_t i = 0; i < n; i++) {
        uint16_t v = ((mask >> i) & 1u) ? (uint16_t)HIST_NULL_U16 : (uint16_t)(int16_t)HIST_NULL_I16;
        memcpy((uint8_t *)out + i * 2, &v, 2);
    }
}

// True when field `index` of a record holds the sentinel rather than a reading.
static inline bool history_field_is_null(uint8_t kind, size_t index, uint16_t raw)
{
    if ((history_unsigned_mask(kind) >> index) & 1u) {
        return raw == (uint16_t)HIST_NULL_U16;
    }
    return (int16_t)raw == (int16_t)HIST_NULL_I16;
}

#ifdef __cplusplus
}
#endif
