// Host tests for history_totals.c's defrost accounting (TASK-41): what the History page's
// "Electricity in" card shows in brackets, via /api/history's "defrost" object.
//
// Built by run.sh; no board needed. The cache and backfill code in history_totals.c is linked but
// not exercised, so the few platform functions it calls are stubbed below.

#include <stdio.h>
#include <string.h>

#include "history_format.h"
#include "history_paths.h"
#include "history_totals.h"
#include "sd_card.h"
#include "time_sync.h"

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                \
    do {                                                           \
        s_checks++;                                                \
        if (!(cond)) {                                             \
            s_failures++;                                          \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

// --- Stubs for the parts of history_totals.c not under test --------------------------------------

void local_date_string(uint32_t ts, char *buf, size_t len)
{
    (void)ts;
    strlcpy(buf, "2026-01-01", len);
}

bool history_path_for_date(uint8_t kind, uint8_t inst, const char *date, char *out, size_t out_len)
{
    (void)kind;
    (void)inst;
    (void)date;
    (void)out;
    (void)out_len;
    return false;
}

bool sd_card_available(void)
{
    return false;
}

bool clock_is_valid(void)
{
    return false;
}

// --- Records ---------------------------------------------------------------------------------------

#define INTERVAL 5

// A slot with every field unknown, as the logger pads an outage and as a converted day's
// defrosting field reads.
static rec_home_t empty(void)
{
    rec_home_t r;
    history_fill_sentinel(KIND_HOME, &r);
    return r;
}

static rec_home_t slot(int16_t heat_w, int16_t elec_w, int16_t heating, int16_t dhw, int16_t defrosting)
{
    rec_home_t r        = empty();
    r.heat_power_w      = heat_w;
    r.elec_power_w      = elec_w;
    r.heating_running   = heating;
    r.dhw_running       = dhw;
    r.defrosting        = defrosting;
    return r;
}

static void add(history_totals_t *t, history_defrost_t *d, rec_home_t r, int times)
{
    for (int i = 0; i < times; i++) {
        history_totals_add(t, (const uint8_t *)&r, INTERVAL);
        history_defrost_add(d, (const uint8_t *)&r, INTERVAL);
    }
}

// --- Tests -----------------------------------------------------------------------------------------

// A day from before defrost was recorded, or a converted one: the flag is the sentinel throughout,
// so the figures are unknown, not zero -- even though the heat pump was running.
static void test_day_without_flag(void)
{
    history_totals_t  t = {0};
    history_defrost_t d = {0};

    add(&t, &d, slot(6000, 1500, 1, 0, HIST_NULL_I16), 100);
    add(&t, &d, empty(), 10);

    CHECK(d.known_slots == 0);
    CHECK(d.defrost_slots == 0);
    CHECK(d.seconds == 0);
    CHECK(d.energy.heat_ws == 0 && d.energy.elec_ws == 0);
    CHECK(t.overall.elec_ws == 1500LL * INTERVAL * 100); // the rest is untouched
}

// Recorded, and no defrost happened: known, and zero.
static void test_day_without_defrost(void)
{
    history_totals_t  t = {0};
    history_defrost_t d = {0};

    add(&t, &d, slot(6000, 1500, 1, 0, 0), 100);

    CHECK(d.known_slots == 100);
    CHECK(d.defrost_slots == 0);
    CHECK(d.energy.elec_ws == 0);
}

// Defrosts inside a heating run and inside a hot water run. The defrost energy is part of the
// day's and the mode's totals, never in addition to them, and its heat is negative.
static void test_defrosts_in_both_modes(void)
{
    history_totals_t  t = {0};
    history_defrost_t d = {0};

    add(&t, &d, slot(6000, 1500, 1, 0, 0), 60);  // heating
    add(&t, &d, slot(-3000, 2000, 1, 0, 1), 24); // 2 min defrost in the heating run
    add(&t, &d, slot(6000, 1500, 1, 0, 0), 60);
    add(&t, &d, slot(5000, 2500, 0, 1, 0), 60);  // hot water
    add(&t, &d, slot(-2000, 1800, 0, 1, 1), 12); // 1 min defrost in the hot water run
    add(&t, &d, slot(0, 20, 0, 0, 0), 30);       // standby

    CHECK(d.known_slots == 60 + 24 + 60 + 60 + 12 + 30);
    CHECK(d.defrost_slots == 36);
    CHECK(d.seconds == 180);
    CHECK(d.energy.elec_ws == (2000LL * 24 + 1800LL * 12) * INTERVAL);
    CHECK(d.energy.heat_ws == (-3000LL * 24 + -2000LL * 12) * INTERVAL);
    CHECK(d.energy.heat_ws < 0);

    // Inside the totals: the defrost slots stay in their mode, which is unchanged.
    CHECK(t.heating.elec_ws == (1500LL * 120 + 2000LL * 24) * INTERVAL);
    CHECK(t.hot_water.elec_ws == (2500LL * 60 + 1800LL * 12) * INTERVAL);
    CHECK(t.overall.elec_ws >= d.energy.elec_ws);
}

// A defrost slot with no electricity reading still counts its time and heat; one with no heat
// reading still counts its electricity.
static void test_missing_power(void)
{
    history_totals_t  t = {0};
    history_defrost_t d = {0};

    add(&t, &d, slot(-3000, HIST_NULL_I16, 1, 0, 1), 4);
    add(&t, &d, slot(HIST_NULL_I16, 2000, 1, 0, 1), 6);

    CHECK(d.defrost_slots == 10);
    CHECK(d.seconds == 50);
    CHECK(d.energy.heat_ws == -3000LL * 4 * INTERVAL);
    CHECK(d.energy.elec_ws == 2000LL * 6 * INTERVAL);
}

// Today, recording started part-way through: only the recorded part is known, and the figures
// cover just that.
static void test_flag_starts_part_way(void)
{
    history_totals_t  t = {0};
    history_defrost_t d = {0};

    add(&t, &d, slot(6000, 1500, 1, 0, HIST_NULL_I16), 50);
    add(&t, &d, slot(-3000, 2000, 1, 0, 1), 6);
    add(&t, &d, slot(6000, 1500, 1, 0, 0), 20);

    CHECK(d.known_slots == 26);
    CHECK(d.energy.elec_ws == 2000LL * 6 * INTERVAL);
}

int main(void)
{
    test_day_without_flag();
    test_day_without_defrost();
    test_defrosts_in_both_modes();
    test_missing_power();
    test_flag_starts_part_way();

    printf("test_totals: %d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
