// Host tests for the parts of the MCP server that are plain C: cycle detection, the point caps and
// timestamp handling. Build and run with ./run.sh -- no ESP-IDF needed.
//
// The night is synthetic but modelled on a real one:
//
//   00:00-01:10  heating, about 3.5 kW heat for 0.7 kW electrical
//   01:35-02:35  hot water, about 6 kW heat for 2.5 kW electrical
//   02:35        changeover straight into heating, with a single 30 kW heat sample as the
//                diverter valve moves
//   02:35-03:10, 03:35-04:10, 04:35-05:10, 05:35-06:00  heating, 35 minutes on and 25 off
//
// and in every off period a pump-only blip: about 2 kW of heat at near-zero electrical.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mcp_cycles.h"
#include "mcp_series.h"
#include "mcp_time.h"

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        s_checks++;                                                              \
        if (!(cond)) {                                                           \
            s_failures++;                                                        \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))

#define STEP_S     5
#define NIGHT      1768435200u // 2026-01-15T00:00:00Z, which is also local midnight
#define AT(h, m)   (NIGHT + (h) * 3600u + (m) * 60u)
#define MAX_RUNS   64

static const cycle_options_t OPTIONS = {
    .on_threshold_w = 300, .min_gap_s = 120, .min_run_s = 60, .rated_output_w = 8000};

static bool within(uint32_t ts, uint32_t from, uint32_t to)
{
    return ts >= from && ts < to;
}

static cycle_sample_t night_sample(uint32_t ts)
{
    bool heating = within(ts, AT(0, 0), AT(1, 10)) || within(ts, AT(2, 35), AT(3, 10)) ||
                   within(ts, AT(3, 35), AT(4, 10)) || within(ts, AT(4, 35), AT(5, 10)) ||
                   within(ts, AT(5, 35), AT(6, 0));
    bool dhw  = within(ts, AT(1, 35), AT(2, 35));
    bool blip = within(ts, AT(1, 20), AT(1, 22)) || within(ts, AT(3, 20), AT(3, 22)) ||
                within(ts, AT(4, 20), AT(4, 22)) || within(ts, AT(5, 20), AT(5, 22));

    cycle_sample_t s = {
        .ts = ts, .step_s = STEP_S,
        .has_elec = true, .has_heat = true, .has_flow = true, .has_return = true, .has_outdoor = true,
        .elec_w       = dhw ? 2500 : heating ? 700 : 20,
        .heat_w       = ts == AT(2, 35) ? 30000 : dhw ? 6000 : heating ? 3500 : blip ? 2000 : 0,
        .flow_c100    = dhw ? 5200 : heating ? 3500 : 2800,
        .return_c100  = dhw ? 4600 : heating ? 3000 : 2700,
        .outdoor_c100 = 400,
        .heating      = heating ? 1 : 0,
        .dhw          = dhw ? 1 : 0,
    };
    return s;
}

static double minutes_in(uint32_t ts)
{
    return (ts - NIGHT) / 60.0;
}

static double cop(const cycle_run_t *run)
{
    return (double)run->cop_heat_ws / (double)run->cop_elec_ws;
}

static void test_night(void)
{
    cycle_run_t   runs[MAX_RUNS];
    cycle_state_t state;
    cycle_init(&state, &OPTIONS, NIGHT, AT(6, 0), runs, MAX_RUNS);

    for (uint32_t ts = NIGHT; ts < AT(6, 0); ts += STEP_S) {
        cycle_sample_t s = night_sample(ts);
        cycle_add_sample(&state, &s);
    }
    cycle_finish(&state);

    // The six runs and nothing else: no pump-only blip became one.
    static const struct { double from, to; cycle_mode_t mode; } EXPECTED[] = {
        {0, 70, CYCLE_MODE_HEATING},    {95, 155, CYCLE_MODE_DHW},      {155, 190, CYCLE_MODE_HEATING},
        {215, 250, CYCLE_MODE_HEATING}, {275, 310, CYCLE_MODE_HEATING}, {335, 360, CYCLE_MODE_HEATING},
    };

    CHECK(state.count == 6);
    CHECK(!state.truncated);
    for (size_t i = 0; i < 6 && i < state.count; i++) {
        CHECK(minutes_in(runs[i].start_ts) == EXPECTED[i].from);
        CHECK(minutes_in(runs[i].end_ts) == EXPECTED[i].to);
        CHECK(runs[i].mode == EXPECTED[i].mode);
    }

    // Hot water is split from the heating it changes over into, which is not a compressor start.
    CHECK(runs[1].off_after_s == 0);
    CHECK(runs[1].compressor_start);
    CHECK(!runs[2].compressor_start);

    // The 30 kW changeover spike is out of that run's heat figures, and only that run lost one.
    CHECK(runs[2].excluded == 1);
    CHECK(runs[2].max_heat_w == 3500);
    CHECK(runs[2].heat_sum_w / runs[2].heat_n == 3500);
    CHECK_NEAR(cop(&runs[2]), 5.0, 1e-9);
    CHECK_NEAR(runs[2].heat_ws / 3.6e6, 3.5 * (35 * 60 - STEP_S) / 3600.0, 1e-9);
    for (size_t i = 0; i < state.count; i++) {
        CHECK(i == 2 || runs[i].excluded == 0);
    }

    // Per-run energy, COP and temperatures.
    CHECK(runs[0].elec_sum_w / runs[0].elec_n == 700);
    CHECK_NEAR(runs[0].heat_ws / 3.6e6, 3.5 * 70 / 60, 1e-9);
    CHECK_NEAR(runs[0].elec_ws / 3.6e6, 0.7 * 70 / 60, 1e-9);
    CHECK_NEAR(cop(&runs[0]), 5.0, 1e-9);
    CHECK(runs[0].flow_sum_c100 / runs[0].flow_n == 3500);
    CHECK(runs[0].return_sum_c100 / runs[0].return_n == 3000);
    CHECK(runs[0].flow_at_stop_c100 == 3500);
    CHECK(runs[0].off_after_s == 25 * 60);
    CHECK_NEAR(cop(&runs[1]), 2.4, 1e-9);
    CHECK(runs[1].flow_at_stop_c100 == 5200);

    // Cut by the period at both ends; the last run has no off time.
    CHECK(runs[0].clipped && runs[5].clipped);
    CHECK(!runs[1].clipped && !runs[2].clipped && !runs[3].clipped && !runs[4].clipped);
    CHECK(runs[5].off_after_s == -1);

    cycle_summary_t summary;
    CHECK(cycle_summarise(&state, &summary));

    CHECK_NEAR(summary.period_hours, 6.0, 1e-9);
    CHECK_NEAR(summary.data_coverage_pct, 100.0, 1e-9);
    CHECK(summary.mode_recorded);
    CHECK(summary.runs == 6 && summary.heating_runs == 5 && summary.dhw_runs == 1);
    // Four compressor starts into heating in six hours; the changeover at 02:35 is not one.
    CHECK_NEAR(summary.heating_starts_per_hour, 4.0 / 6.0, 1e-9);
    CHECK(summary.has_median_heating_run && summary.median_heating_run_min == 35);
    CHECK(summary.has_median_off && summary.median_off_min == 25);
    CHECK_NEAR(summary.cop_heating, 5.0, 1e-9);
    CHECK_NEAR(summary.cop_dhw, 2.4, 1e-9);
    CHECK_NEAR(summary.dhw_heat_kwh, 6.0, 1e-9);
    CHECK_NEAR(summary.dhw_elec_kwh, 2.5, 1e-9);
    CHECK_NEAR(summary.heating_elec_kwh, 0.7 * 200 / 60, 1e-9);
    CHECK_NEAR(summary.elec_kwh, 2.5 + 0.7 * 200 / 60, 1e-9);
    // 100 minutes off at 20 W of standby.
    CHECK_NEAR(summary.elec_kwh_outside_runs, 0.02 * 100 / 60, 1e-9);
    CHECK_NEAR(summary.mean_outdoor_c, 4.0, 1e-9);
    CHECK(summary.excluded_heat_samples == 1);
}

// Feeds stretches of constant electrical input, with no mode recorded, and returns the run count.
typedef struct { uint32_t seconds; int32_t elec_w; uint32_t hole_before_s; } stretch_t;

static size_t run_stretches(const stretch_t *stretches, size_t n, cycle_run_t *runs, cycle_state_t *state)
{
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        total += stretches[i].seconds + stretches[i].hole_before_s;
    }

    cycle_init(state, &OPTIONS, 0, total, runs, MAX_RUNS);

    uint32_t ts = 0;
    for (size_t i = 0; i < n; i++) {
        ts += stretches[i].hole_before_s;
        for (uint32_t t = 0; t < stretches[i].seconds; t += STEP_S, ts += STEP_S) {
            cycle_sample_t s = {.ts = ts, .step_s = STEP_S, .has_elec = true, .has_heat = true,
                                .elec_w = stretches[i].elec_w, .heat_w = stretches[i].elec_w * 4,
                                .heating = -1, .dhw = -1};
            cycle_add_sample(state, &s);
        }
    }
    cycle_finish(state);
    return state->count;
}

static void test_run_boundaries(void)
{
    cycle_run_t   runs[MAX_RUNS];
    cycle_state_t state;

    // A gap shorter than min_gap_s is part of the run.
    const stretch_t merged[] = {{600, 800, 0}, {60, 50, 0}, {600, 800, 0}};
    CHECK(run_stretches(merged, 3, runs, &state) == 1);
    CHECK(runs[0].end_ts - runs[0].start_ts == 21 * 60);

    // One of min_gap_s or longer is not.
    const stretch_t apart[] = {{600, 800, 0}, {120, 50, 0}, {600, 800, 0}};
    CHECK(run_stretches(apart, 3, runs, &state) == 2);
    CHECK(runs[0].off_after_s == 120);
    CHECK(runs[0].end_ts == 600);

    // A run shorter than min_run_s is dropped.
    const stretch_t shorty[] = {{300, 50, 0}, {30, 1000, 0}, {300, 50, 0}, {600, 800, 0}};
    CHECK(run_stretches(shorty, 4, runs, &state) == 1);
    CHECK(runs[0].start_ts == 630);

    // With no mode recorded a run's mode is unknown, and it is not a heating run.
    const stretch_t plain[] = {{300, 50, 0}, {600, 800, 0}, {300, 50, 0}};
    CHECK(run_stretches(plain, 3, runs, &state) == 1);
    CHECK(runs[0].mode == CYCLE_MODE_UNKNOWN);
    CHECK(!runs[0].clipped);

    cycle_summary_t summary;
    CHECK(cycle_summarise(&state, &summary));
    CHECK(!summary.mode_recorded && summary.heating_runs == 0);
    CHECK(!summary.has_cop_heating && summary.has_cop_all);

    // A hole in the data ends a run.
    const stretch_t holed[] = {{600, 800, 0}, {600, 800, 3600}};
    CHECK(run_stretches(holed, 2, runs, &state) == 2);

    // More runs than there is room for are counted as truncated, not written past the end.
    stretch_t many[2 * (MAX_RUNS + 4)];
    for (size_t i = 0; i < MAX_RUNS + 4; i++) {
        many[2 * i]     = (stretch_t){300, 800, 0};
        many[2 * i + 1] = (stretch_t){300, 50, 0};
    }
    CHECK(run_stretches(many, 2 * (MAX_RUNS + 4), runs, &state) == MAX_RUNS);
    CHECK(state.truncated);
}

static void test_mode_flicker(void)
{
    cycle_run_t   runs[MAX_RUNS];
    cycle_state_t state;
    cycle_init(&state, &OPTIONS, 0, 1800, runs, MAX_RUNS);

    // Heating throughout, bar 20 s where the flag reads hot water and 30 s where it reads neither.
    for (uint32_t ts = 0; ts < 1800; ts += STEP_S) {
        bool flicker = ts >= 600 && ts < 620;
        bool neither = ts >= 900 && ts < 930;
        cycle_sample_t s = {.ts = ts, .step_s = STEP_S, .has_elec = true, .has_heat = true,
                            .elec_w = 700, .heat_w = 3500,
                            .heating = (flicker || neither) ? 0 : 1, .dhw = flicker ? 1 : 0};
        cycle_add_sample(&state, &s);
    }
    cycle_finish(&state);

    CHECK(state.count == 1);
    CHECK(runs[0].mode == CYCLE_MODE_HEATING);
    CHECK(runs[0].end_ts - runs[0].start_ts == 1800);
}

static void test_plausibility(void)
{
    // Above three times the rated output.
    CHECK(!cycle_heat_is_plausible(24001, true, 3000, 8000));
    CHECK(cycle_heat_is_plausible(24000, true, 3000, 8000));

    // An instantaneous COP above 10, including heat for no electricity.
    CHECK(!cycle_heat_is_plausible(7001, true, 700, 8000));
    CHECK(cycle_heat_is_plausible(7000, true, 700, 8000));
    CHECK(!cycle_heat_is_plausible(2000, true, 0, 8000));

    // Nothing to judge it by, and heat being taken from the house.
    CHECK(cycle_heat_is_plausible(3500, false, 0, 8000));
    CHECK(cycle_heat_is_plausible(-1500, true, 900, 8000));
}

static void test_point_caps(void)
{
    uint32_t interval = 0;

    // A day with no interval named: at most 1000 points, on a whole number of samples.
    CHECK(mcp_plan_interval(86400, 0, 5, &interval) == MCP_PLAN_OK);
    CHECK(interval == 90);
    CHECK(86400 / interval <= MCP_AUTO_MAX_POINTS);

    // Never finer than the samples themselves.
    CHECK(mcp_plan_interval(600, 0, 5, &interval) == MCP_PLAN_OK && interval == 5);

    // A month.
    CHECK(mcp_plan_interval(31 * 86400, 0, 5, &interval) == MCP_PLAN_OK);
    CHECK((31 * 86400 + interval - 1) / interval <= MCP_AUTO_MAX_POINTS);

    // Over 5000 points is refused, with the interval that would fit.
    CHECK(mcp_plan_interval(86400, 5, 5, &interval) == MCP_PLAN_TOO_MANY_POINTS);
    CHECK(interval == 18);

    // Exactly 5000 is allowed; one second more is not.
    CHECK(mcp_plan_interval(MCP_HARD_MAX_POINTS * 5, 5, 5, &interval) == MCP_PLAN_OK && interval == 5);
    CHECK(mcp_plan_interval(MCP_HARD_MAX_POINTS * 5 + 1, 5, 5, &interval) == MCP_PLAN_TOO_MANY_POINTS);

    CHECK(mcp_plan_interval(0, 0, 5, &interval) == MCP_PLAN_EMPTY_RANGE);

    CHECK(mcp_series_find("heat_output_w") != NULL);
    CHECK(mcp_series_find("heating_running")->field == 11);
    CHECK(mcp_series_find("target_flow_c") == NULL);

    mcp_series_ref_t ref;
    CHECK(mcp_series_resolve("heating_running", &ref) && ref.source == MCP_SOURCE_HOME && ref.field == 11);
    CHECK(mcp_series_resolve("room_4_temp_c", &ref) && ref.source == MCP_SOURCE_ROOM && ref.inst == 4 &&
          ref.field == 0 && ref.divisor == 100);
    CHECK(mcp_series_resolve("radiator_12_flow_temp_c", &ref) && ref.source == MCP_SOURCE_RADIATOR &&
          ref.inst == 12 && ref.field == 0);
    CHECK(mcp_series_resolve("radiator_255_return_temp_c", &ref) && ref.inst == 255 && ref.field == 1);
    CHECK(mcp_series_resolve("room_0_temp_c", &ref) && ref.inst == 0);
    CHECK(!mcp_series_resolve("room_256_temp_c", &ref));
    CHECK(!mcp_series_resolve("room_01_temp_c", &ref));
    CHECK(!mcp_series_resolve("room__temp_c", &ref));
    CHECK(!mcp_series_resolve("room_+1_temp_c", &ref));
    CHECK(!mcp_series_resolve("room_1_temp_c ", &ref));
    CHECK(!mcp_series_resolve("room_1_flow_temp_c", &ref));
    CHECK(!mcp_series_resolve("radiator_3_temp_c", &ref));
}

static void test_time(void)
{
    // The zone time_sync.c sets on the device.
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    uint32_t ts = 0;
    char     buf[32];

    // No offset: wall-clock time in the house.
    CHECK(mcp_time_parse("2026-07-01T00:00", false, &ts) && ts == 1782860400u); // 2026-06-30T23:00Z
    CHECK(mcp_time_parse("2026-01-15T00:00:00", false, &ts) && ts == NIGHT);
    CHECK(mcp_time_parse("2026-07-01", false, &ts) && ts == 1782860400u);
    CHECK(mcp_time_parse("2026-07-01T00:00", true, &ts) && ts == 1782864000u);

    // An explicit offset is taken as given.
    CHECK(mcp_time_parse("2026-07-01T00:00:00Z", false, &ts) && ts == 1782864000u);
    CHECK(mcp_time_parse("2026-07-01T00:00:00+02:00", false, &ts) && ts == 1782856800u);
    CHECK(mcp_time_parse("2026-07-01T00:00:00.250-0100", false, &ts) && ts == 1782867600u);

    CHECK(!mcp_time_parse("yesterday", false, &ts));
    CHECK(!mcp_time_parse("2026-13-01", false, &ts));
    CHECK(!mcp_time_parse("2026-07-01T00:00 please", false, &ts));

    mcp_time_format(1782860400u, false, buf, sizeof(buf));
    CHECK(strcmp(buf, "2026-07-01T00:00:00+01:00") == 0);
    mcp_time_format(NIGHT + 6 * 3600 + 1800, false, buf, sizeof(buf));
    CHECK(strcmp(buf, "2026-01-15T06:30:00+00:00") == 0);
    mcp_time_format(1782860400u, true, buf, sizeof(buf));
    CHECK(strcmp(buf, "2026-06-30T23:00:00Z") == 0);
}

int main(void)
{
    test_night();
    test_run_boundaries();
    test_mode_flicker();
    test_plausibility();
    test_point_caps();
    test_time();

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
