// Tests heat_balance_core.h: what the heat balance counts, and how a bucket ends (TASK-45). The
// buckets here are an hour long; the device uses 10 minutes, and the arithmetic does not care.
#include <stdio.h>

#include "heat_balance_core.h"

static int checks, failures;

#define CHECK_EQ(actual, expected)                                                                     \
    do {                                                                                               \
        long long a_ = (long long)(actual), e_ = (long long)(expected);                                \
        checks++;                                                                                      \
        if (a_ != e_) {                                                                                \
            failures++;                                                                                \
            printf("%s:%d: %s = %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_);          \
        }                                                                                              \
    } while (0)

static hb_sample_t heating(int32_t meter, int32_t rads, int32_t ufh)
{
    hb_sample_t s = {true, false, false, true, true, true, meter, rads, ufh};
    return s;
}

int main(void)
{
    const uint32_t H0 = 1795960800; // an hour start
    const uint32_t H1 = H0 + 3600;
    hb_bucket_t cur;
    hb_bucket_t done;
    memset(&cur, 0, sizeof(cur));

    // The first sample only starts an hour.
    hb_sample_t s = heating(7000, 4200, 2800);
    CHECK_EQ(hb_add(&cur, H0, 5, &s, &done), 0);
    CHECK_EQ(cur.start, H0);
    CHECK_EQ(cur.counted_s, 5);
    CHECK_EQ(cur.heat_meter_j, 35000);
    CHECK_EQ(cur.radiators_j, 21000);
    CHECK_EQ(cur.ufh_j, 14000);

    // Hot water, a defrost and the heat pump being off count for nothing at all.
    hb_sample_t dhw = heating(9000, 0, 0);
    dhw.dhw = true;
    hb_sample_t defrost = heating(-6800, 0, 0);
    defrost.defrosting = true;
    hb_sample_t off = heating(0, 0, 0);
    off.heating = false;
    CHECK_EQ(hb_add(&cur, H0, 5, &dhw, &done), 0);
    CHECK_EQ(hb_add(&cur, H0, 5, &defrost, &done), 0);
    CHECK_EQ(hb_add(&cur, H0, 5, &off, &done), 0);
    CHECK_EQ(cur.counted_s, 5);
    CHECK_EQ(cur.unknown_s, 0);
    CHECK_EQ(cur.heat_meter_j, 35000);

    // Heating with a figure missing goes to unknown_s and adds no energy.
    hb_sample_t no_ufh = heating(7000, 4200, 0);
    no_ufh.ufh_known = false;
    hb_sample_t no_meter = heating(0, 4200, 2800);
    no_meter.heat_meter_known = false;
    hb_sample_t no_rads = heating(7000, 0, 2800);
    no_rads.radiators_known = false;
    CHECK_EQ(hb_add(&cur, H0, 5, &no_ufh, &done), 0);
    CHECK_EQ(hb_add(&cur, H0, 5, &no_meter, &done), 0);
    CHECK_EQ(hb_add(&cur, H0, 5, &no_rads, &done), 0);
    CHECK_EQ(cur.unknown_s, 15);
    CHECK_EQ(cur.counted_s, 5);
    CHECK_EQ(cur.radiators_j, 21000);

    // Fill the rest of the hour: 718 more samples of 5 s at 7000 / 4200 / 2800 W.
    for (int i = 0; i < 718; i++) {
        CHECK_EQ(hb_add(&cur, H0, 5, &s, &done), 0);
    }
    CHECK_EQ(cur.counted_s, 3595);

    // The next hour's first sample finishes this one and starts the next with itself.
    hb_sample_t s2 = heating(6000, 3000, 3000);
    CHECK_EQ(hb_add(&cur, H1, 5, &s2, &done), 1);
    CHECK_EQ(done.start, H0);
    CHECK_EQ(done.counted_s, 3595);
    CHECK_EQ(cur.start, H1);
    CHECK_EQ(cur.counted_s, 5);
    CHECK_EQ(cur.heat_meter_j, 30000);

    // The record: joules to the nearest Wh. 7000 W for 3595 s is 6990.3 Wh.
    hb_record_t rec = hb_record(&done);
    CHECK_EQ(sizeof(rec), 32);
    CHECK_EQ(rec.start, H0);
    CHECK_EQ(rec.counted_s, 3595);
    CHECK_EQ(rec.unknown_s, 15);
    CHECK_EQ(rec.heat_meter_wh, 6990);
    CHECK_EQ(rec.radiators_wh, 4194);
    CHECK_EQ(rec.ufh_wh, 2796);
    CHECK_EQ(rec.reserved[0], 0);
    CHECK_EQ(rec.reserved[11], 0);

    // Negative energy rounds the same way: -1800 J is -0.5 Wh, which rounds away from zero.
    CHECK_EQ(hb_wh(-1800), -1);
    CHECK_EQ(hb_wh(-1799), 0);
    CHECK_EQ(hb_wh(1800), 1);

    // A jump of more than an hour (the device was off) finishes the old hour as it stood.
    hb_bucket_t later;
    CHECK_EQ(hb_add(&cur, H1 + 3 * 3600, 5, &s, &later), 1);
    CHECK_EQ(later.start, H1);
    CHECK_EQ(later.counted_s, 5);

    printf("test_heat_balance: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
