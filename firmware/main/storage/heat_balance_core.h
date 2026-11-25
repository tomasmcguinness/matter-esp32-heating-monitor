#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The heat balance's arithmetic (TASK-45): what counts, and when a bucket ends. Plain C with
// no ESP-IDF in it, so test_host/test_heat_balance.c tests it on a desktop; heat_balance.cpp feeds
// it from the history sampler and writes what it hands back.
//
// The balance compares the heat meter with the live model of where that heat goes: radiators by
// their dT50 rating, UFH manifolds by calibrated loop flow x the drop across them. Those only
// describe the same water while the heat pump is space heating, so a sample counts only then: not
// while it makes hot water (the meter is measuring the cylinder) and not while it defrosts (heat is
// flowing back out of the system, and both models report 0 W).

// One sample from the sampler, `seconds` long.
typedef struct {
    bool heating;     // heating running
    bool dhw;         // hot water running
    bool defrosting;  // home_is_defrosting()
    bool heat_meter_known;
    bool radiators_known;
    bool ufh_known;
    int32_t heat_meter_w;
    int32_t radiators_w;
    int32_t ufh_w;
} hb_sample_t;

// One bucket of local clock time (10 minutes on the device), being added up. Energies in joules, so
// a 5 s sample loses nothing to rounding; they become Wh only in the record.
typedef struct {
    uint32_t start;        // Unix time of the bucket's start; 0 until the first sample
    uint32_t counted_s;    // space heating, with all three figures known
    uint32_t unknown_s;    // space heating, but a figure was missing
    int64_t heat_meter_j;
    int64_t radiators_j;
    int64_t ufh_j;
} hb_bucket_t;

// The record each completed bucket is stored as. Fixed at 32 bytes; reserved is zero, for fields
// added later without a version change.
typedef struct __attribute__((packed)) {
    uint32_t start;
    uint16_t counted_s;
    uint16_t unknown_s;
    int32_t heat_meter_wh;
    int32_t radiators_wh;
    int32_t ufh_wh;
    uint8_t reserved[12];
} hb_record_t;

#ifdef __cplusplus
static_assert(sizeof(hb_record_t) == 32, "hb_record_t must stay 32 bytes");
#else
_Static_assert(sizeof(hb_record_t) == 32, "hb_record_t must stay 32 bytes");
#endif

// Adds a sample taken in the bucket starting at `start`. If that is a new bucket, the one being added
// up is finished first: it is copied to *done and true is returned, and the sample starts the new
// one. The first sample after start-up only starts a bucket.
static inline bool hb_add(hb_bucket_t *cur, uint32_t start, uint32_t seconds, const hb_sample_t *s, hb_bucket_t *done)
{
    bool finished = false;

    if (cur->start != start)
    {
        if (cur->start != 0)
        {
            *done = *cur;
            finished = true;
        }

        memset(cur, 0, sizeof(*cur));
        cur->start = start;
    }

    bool space_heating = s->heating && !s->dhw && !s->defrosting;

    if (!space_heating)
    {
        return finished;
    }

    if (s->heat_meter_known && s->radiators_known && s->ufh_known)
    {
        cur->counted_s += seconds;
        cur->heat_meter_j += (int64_t)s->heat_meter_w * seconds;
        cur->radiators_j += (int64_t)s->radiators_w * seconds;
        cur->ufh_j += (int64_t)s->ufh_w * seconds;
    }
    else
    {
        cur->unknown_s += seconds;
    }

    return finished;
}

// Joules to the nearest Wh, either sign.
static inline int32_t hb_wh(int64_t joules)
{
    return (int32_t)(joules >= 0 ? (joules + 1800) / 3600 : (joules - 1800) / 3600);
}

static inline hb_record_t hb_record(const hb_bucket_t *bucket)
{
    hb_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.start = bucket->start;
    rec.counted_s = (uint16_t)(bucket->counted_s > 65535 ? 65535 : bucket->counted_s);
    rec.unknown_s = (uint16_t)(bucket->unknown_s > 65535 ? 65535 : bucket->unknown_s);
    rec.heat_meter_wh = hb_wh(bucket->heat_meter_j);
    rec.radiators_wh = hb_wh(bucket->radiators_j);
    rec.ufh_wh = hb_wh(bucket->ufh_j);
    return rec;
}
