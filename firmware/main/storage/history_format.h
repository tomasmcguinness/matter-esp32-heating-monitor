#pragma once

#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

// On-disk format for the SD card history.
//
// Files are per local day, per sensor:
//
//   /sdcard/sensor-<nodeId>-<endpointId>-YYYY-MM-DD   raw per-sensor archive
//   /sdcard/home-YYYY-MM-DD                           resolved home view
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
#define HISTORY_VERSION 1

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

typedef enum {
    KIND_TEMPERATURE = 1,
    KIND_FLOW        = 2,
    KIND_ELECTRICAL  = 3,
    KIND_HEAT_METER  = 4,
    KIND_HOME        = 5,
} sensor_kind_t;

// "No reading in this slot". Both sit outside any physical range -- INT16_MIN as
// 0.01 degC is -327.68 degC. Zero cannot be used: 0 W is a real electrical reading, a
// distinction the UI already makes (ElectricalPower.tsx renders 0 but dashes on null).
#define HIST_NULL_I16 INT16_MIN
#define HIST_NULL_U16 UINT16_MAX

// Power is stored in whole watts as int16, which caps at +/-32.7 kW. That is well above
// a domestic heat pump but it IS a cap; widen to int32 here if this ever drives
// something industrial.

typedef struct __attribute__((packed)) {
    int16_t temp_c100;          // TemperatureMeasurement MeasuredValue, 0.01 degC
} rec_temperature_t;

typedef struct __attribute__((packed)) {
    uint16_t flow_m3h10;        // FlowMeasurement MeasuredValue, 0.1 m3/h
} rec_flow_t;

typedef struct __attribute__((packed)) {
    int16_t  power_w;           // ActivePower,   mW -> W
    uint16_t voltage_dv;        // Voltage,       mV -> 0.1 V
    uint16_t current_ca;        // ActiveCurrent, mA -> 0.01 A
} rec_electrical_t;

typedef struct __attribute__((packed)) {
    int16_t  power_w;           // Heat Meter power,  mW -> W
    int16_t  flow_temp_c100;    // 0.01 degC
    int16_t  return_temp_c100;  // 0.01 degC
    uint16_t flow_lph;          // Heat Meter flow, l/h
} rec_heat_meter_t;

typedef struct __attribute__((packed)) {
    int16_t  heat_output_w;
    int16_t  elec_power_w;
    int16_t  flow_temp_c100;
    int16_t  return_temp_c100;
    uint16_t flow_m3h10;
    int16_t  outdoor_temp_c100;
} rec_home_t;

HIST_STATIC_ASSERT(sizeof(rec_temperature_t) == 2, "rec_temperature_t must stay 2 bytes");
HIST_STATIC_ASSERT(sizeof(rec_flow_t)        == 2, "rec_flow_t must stay 2 bytes");
HIST_STATIC_ASSERT(sizeof(rec_electrical_t)  == 6, "rec_electrical_t must stay 6 bytes");
HIST_STATIC_ASSERT(sizeof(rec_heat_meter_t)  == 8, "rec_heat_meter_t must stay 8 bytes");
HIST_STATIC_ASSERT(sizeof(rec_home_t)        == 12, "rec_home_t must stay 12 bytes");

// Largest record, for fixed-size buffers in the writer and reader.
#define HISTORY_MAX_RECORD_SIZE 12

static inline size_t history_record_size(uint8_t kind)
{
    switch (kind) {
    case KIND_TEMPERATURE: return sizeof(rec_temperature_t);
    case KIND_FLOW:        return sizeof(rec_flow_t);
    case KIND_ELECTRICAL:  return sizeof(rec_electrical_t);
    case KIND_HEAT_METER:  return sizeof(rec_heat_meter_t);
    case KIND_HOME:        return sizeof(rec_home_t);
    default:               return 0;
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
static inline uint16_t history_unsigned_mask(uint8_t kind)
{
    switch (kind) {
    case KIND_TEMPERATURE: return 0x00; // [i16]
    case KIND_FLOW:        return 0x01; // [u16]
    case KIND_ELECTRICAL:  return 0x06; // [i16, u16, u16]
    case KIND_HEAT_METER:  return 0x08; // [i16, i16, i16, u16]
    case KIND_HOME:        return 0x10; // [i16, i16, i16, i16, u16, i16]
    default:               return 0x00;
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
