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
// The version is per kind -- see history_version(). The home record was widened in version 3;
// rooms and radiators never changed and are still version 2, so their files were not touched.
//
// Slot arithmetic is pure unix arithmetic against base_ts, so a 23- or 25-hour local
// day at a DST change simply has fewer or more slots. Nothing may assume 17280.

#ifdef __cplusplus
extern "C" {
#endif

#define HISTORY_MAGIC   0x31534D48u // 'HMS1' little-endian

// There is no single HISTORY_VERSION: each kind has its own, from history_version() below, so the
// home record could be widened without making every room and radiator file unreadable.
#define HISTORY_HOME_VERSION     3
#define HISTORY_ROOM_VERSION     2
#define HISTORY_RADIATOR_VERSION 2

// The home record as firmware before version 3 wrote it: 28 bytes, which are exactly the first 28
// of today's rec_home_t (asserted below). The firmware never reads or writes this layout. It is
// here for the offline converter (history_convert/), and so that a day file still in it can be
// recognised and reported rather than silently skipped -- see history_check_header().
#define HISTORY_HOME_V2_VERSION     2
#define HISTORY_HOME_V2_RECORD_SIZE 28

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
    KIND_HOME     = 5,
    KIND_ROOM     = 6,
    KIND_RADIATOR = 7,
} sensor_kind_t;

// KIND_HOME is a singleton, but a room and a radiator are instances: there are many of each
// and every one needs its own series. Everything below that is switched on the kind therefore
// takes an instance id alongside it -- see history_paths.h, where the id becomes a directory.

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
//
// Version 3 widened the record from 28 to 40 bytes, for the defrost flag (TASK-40). The 14 fields
// of version 2 are unchanged and in the same places, reserved1 and reserved2 included; defrosting
// and five new spares were appended after them. Days recorded before the widening are converted
// off the device by history_convert/, which copies each record's 28 bytes and fills the new fields
// with the sentinel, so a converted day reads defrosting as unknown, not as "not defrosting".
// Widening again means another version, another converter pass over the card, and every reader
// revisited -- spend the reserved fields first.
typedef struct __attribute__((packed)) {
    int16_t  heat_power_w;        // Heat Meter power,        mW -> W
    int16_t  elec_power_w;        // ElectricalPower.ActivePower, mW -> W
    int16_t  flow_temp_c100;      // 0.01 degC
    int16_t  return_temp_c100;    // 0.01 degC
    uint16_t flow_lph;            // Heat Meter flow, l/h
    int16_t  outdoor_temp_c100;   // 0.01 degC
    int16_t  average_internal_temp_c100; // 0.01 degC, mean of room temps, derived by update_home()
    int16_t  cop_x100;            // derived by update_home(), 0.01
    int16_t  dhw_running;         // 0 or 1, heat pump producing hot water
    uint16_t elec_voltage_dv;     // Voltage,       mV -> 0.1 V
    uint16_t elec_current_ca;     // ActiveCurrent, mA -> 0.01 A
    int16_t  heating_running;     // 0 or 1, heat pump space heating
    int16_t  reserved1;
    int16_t  reserved2;
    // ---- version 3 from here ----
    // 0 or 1, heat pump defrosting its outdoor coil. Independent of the two above: a defrost
    // happens inside a heating run or a hot water run and leaves that run's flag as it was.
    int16_t  defrosting;
    int16_t  reserved3;
    int16_t  reserved4;
    int16_t  reserved5;
    int16_t  reserved6;
    int16_t  reserved7;
} rec_home_t;

HIST_STATIC_ASSERT(sizeof(rec_home_t) == 40, "rec_home_t must stay 40 bytes");
HIST_STATIC_ASSERT(offsetof(rec_home_t, defrosting) == HISTORY_HOME_V2_RECORD_SIZE,
                   "the version 2 home record must be exactly the prefix before defrosting");

// Rooms and radiators record MEASUREMENTS ONLY -- no heat output, no heat loss, no mean water
// temperature. Every one of those is a pure function of the temperatures below plus
// configuration that already lives in NVS (a radiator's output_dt_50, a room's
// predicted_heat_loss_per_degree and target, and which radiators are in which room), so
// calculations_manager's arithmetic can be replayed at read time from these columns.
//
// That is deliberate, and it is what makes the archive worth keeping. Storing a derived value
// freezes the formula that produced it: the heat-loss maths carried a sign bug until
// 2026-09-14 that inflated every figure taken below freezing, and had those numbers been on
// the card they would have stayed wrong for as long as the card was kept. Measurements do not
// go stale when the code improves.
//
// The cost of deriving rather than storing is that the configuration is NOT versioned here.
// Re-rate a radiator or move it to another room and every historical figure computed from it
// changes silently. Write a per-day sidecar of the config if that matters.

typedef struct __attribute__((packed)) {
    int16_t current_temp_c100;   // room air temperature, 0.01 degC
    int16_t reserved0;           // earmarked for age_s -- see the freshness note below
    int16_t reserved1;
    int16_t reserved2;
} rec_room_t;

HIST_STATIC_ASSERT(sizeof(rec_room_t) == 8, "rec_room_t must stay 8 bytes");

typedef struct __attribute__((packed)) {
    int16_t flow_temp_c100;      // 0.01 degC
    int16_t return_temp_c100;    // 0.01 degC
    int16_t reserved0;           // earmarked for age_s -- see the freshness note below
    int16_t reserved1;
} rec_radiator_t;

HIST_STATIC_ASSERT(sizeof(rec_radiator_t) == 8, "rec_radiator_t must stay 8 bytes");

// dT is flow - return and mean water temperature is their mean, both exact. Neither is stored:
// a stored copy can disagree with its own operands once either has been clamped.
//
// The freshness gap, which matters for anything learning from these series. Sensors are
// subscribed min 0 / max 60 (subscription_manager.cpp), so a value is reported on change and
// otherwise re-sent at most once a minute. A 5 s sampler therefore writes a mix of fresh and
// held readings, and nothing here can tell them apart -- endpoint_entry_t carries no
// last-updated timestamp. A steady column and a sensor that fell over an hour ago look
// identical. Fixing that means stamping endpoint_entry_t in attribute_data_cb and spending
// reserved0 on the age; until then, treat flat runs with suspicion.

// Largest record, for fixed-size buffers in the writer and reader.
#define HISTORY_MAX_RECORD_SIZE 40

// The format version this firmware writes and reads for a kind. 0 for an unknown kind.
static inline uint8_t history_version(uint8_t kind)
{
    switch (kind) {
    case KIND_HOME:     return HISTORY_HOME_VERSION;
    case KIND_ROOM:     return HISTORY_ROOM_VERSION;
    case KIND_RADIATOR: return HISTORY_RADIATOR_VERSION;
    default:            return 0;
    }
}

static inline size_t history_record_size(uint8_t kind)
{
    switch (kind) {
    case KIND_HOME:     return sizeof(rec_home_t);
    case KIND_ROOM:     return sizeof(rec_room_t);
    case KIND_RADIATOR: return sizeof(rec_radiator_t);
    default:            return 0;
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
// The mask is a uint32_t, one bit per field, which caps any record at 32 fields / 64
// bytes. Widen this type before adding a 33rd field.
static inline uint32_t history_unsigned_mask(uint8_t kind)
{
    switch (kind) {
    // flow_lph (4), elec_voltage_dv (9), elec_current_ca (10)
    case KIND_HOME: return 0x0610;
    // Every field of both is a signed temperature, so the sentinel is HIST_NULL_I16 throughout.
    case KIND_ROOM:
    case KIND_RADIATOR: return 0x0000;
    default:            return 0x0000;
    }
}

// Fills a record with "no reading" for every field. Used to pad slots the logger missed,
// so that offset = header + slot * record_size stays true across an outage.
static inline void history_fill_sentinel(uint8_t kind, void *out)
{
    uint32_t mask = history_unsigned_mask(kind);
    size_t   n    = history_field_count(kind);

    for (size_t i = 0; i < n; i++) {
        uint16_t v = ((mask >> i) & 1u) ? (uint16_t)HIST_NULL_U16 : (uint16_t)(int16_t)HIST_NULL_I16;
        memcpy((uint8_t *)out + i * 2, &v, 2);
    }
}

// True when field `index` of a record holds the sentinel rather than a reading.
//
// Only the sentinel is null, for every kind: 0 is a real temperature and a real 0 W. Room and
// radiator files written before 2026-10-01 recorded a sensor that had not reported yet as 0, and
// those slots read back as 0.00 degC.
static inline bool history_field_is_null(uint8_t kind, size_t index, uint16_t raw)
{
    if ((history_unsigned_mask(kind) >> index) & 1u) {
        return raw == (uint16_t)HIST_NULL_U16;
    }
    return (int16_t)raw == (int16_t)HIST_NULL_I16;
}

// What a day file's header says about whether this firmware can read it.
typedef enum {
    HISTORY_HEADER_OK,         // this firmware's format for the kind
    HISTORY_HEADER_OLD_FORMAT, // a version 2 home day, recorded before the widening: convert it
    HISTORY_HEADER_BAD,        // anything else -- not a history file, another kind, or unknown
} history_header_status_t;

// Checks a header against the kind the caller expects. Every reader and the writer go through
// this, so a home day still in the old layout is reported as such everywhere, and never read
// with the wrong record size or appended to.
static inline history_header_status_t history_check_header(const history_header_t *hdr, uint8_t kind)
{
    if (hdr->magic != HISTORY_MAGIC || hdr->sensor_kind != kind || hdr->interval_s == 0) {
        return HISTORY_HEADER_BAD;
    }
    if (hdr->version == history_version(kind) && hdr->record_size == history_record_size(kind)) {
        return HISTORY_HEADER_OK;
    }
    if (kind == KIND_HOME && hdr->version == HISTORY_HOME_V2_VERSION &&
        hdr->record_size == HISTORY_HOME_V2_RECORD_SIZE) {
        return HISTORY_HEADER_OLD_FORMAT;
    }
    return HISTORY_HEADER_BAD;
}

#ifdef __cplusplus
}
#endif
