#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Compressor run detection for the MCP server's summarise_cycles and compare_periods tools.
//
// Plain C with no ESP-IDF in it, so test_host/ can drive it on a desktop with a synthetic night.
//
// A run is found from electrical input alone: the compressor is the only thing on the meter that
// draws hundreds of watts, whereas heat goes on arriving after it stops -- the circulation pump
// keeps moving warm water -- and so says little about when it ran.
//
// Samples are fed one at a time, in time order, straight off the day files. Nothing here holds
// the samples: a run is a set of running sums, so a month costs the same memory as a night.

#ifdef __cplusplus
extern "C" {
#endif

// A heat sample is not believed if it is more than this many times the unit's rated output, or
// implies an instantaneous COP above the limit. Both are what a heat meter reports for a moment
// when a diverter valve moves and it sees the tank's temperature against the heating loop's flow.
#define CYCLE_RATED_OUTPUT_MULTIPLE 3
#define CYCLE_MAX_PLAUSIBLE_COP     10

// Mode changes kept within one compressor run before further ones are folded into the last. A
// run changes mode once or twice; this is headroom for a flag that flickers.
#define CYCLE_MAX_SEGMENTS 16

typedef enum {
    CYCLE_MODE_NONE = 0, // not yet known; never appears in a finished run
    CYCLE_MODE_HEATING,
    CYCLE_MODE_DHW,
    CYCLE_MODE_OTHER,    // the mode was being recorded, and it was neither
    CYCLE_MODE_UNKNOWN,  // the mode was not being recorded
} cycle_mode_t;

typedef struct {
    uint32_t on_threshold_w;
    uint32_t min_gap_s;
    uint32_t min_run_s;
    uint32_t rated_output_w;
} cycle_options_t;

// One slot of the home series. The has_* flags are false where the record held the sentinel.
typedef struct {
    uint32_t ts;
    uint32_t step_s;
    bool     has_elec, has_heat, has_flow, has_return, has_outdoor;
    int32_t  elec_w, heat_w;
    int32_t  flow_c100, return_c100, outdoor_c100;
    int8_t   heating, dhw; // 0 or 1, -1 where the mode was not recorded
} cycle_sample_t;

// A finished run, as the sums it was built from. The figures a caller reports -- averages, kWh,
// COP -- are ratios of these, and keeping the sums is what lets the period summary add runs
// together exactly rather than averaging averages.
typedef struct {
    uint32_t     start_ts;
    uint32_t     end_ts;
    cycle_mode_t mode;
    // False for a run that carries straight on from the one before it in another mode, with the
    // compressor never stopping: a changeover, not a start.
    bool         compressor_start;
    // The run was already under way at the start of the period, or still going at its end.
    bool         clipped;

    // Heat, over the samples that passed cycle_heat_is_plausible().
    int64_t  heat_sum_w;
    uint32_t heat_n;
    int32_t  max_heat_w;
    int64_t  heat_ws;
    uint32_t excluded;

    int64_t  elec_sum_w;
    uint32_t elec_n;
    int64_t  elec_ws;

    // The COP divides energies over the same samples. Electricity from a sample whose heat was
    // thrown out would otherwise count against a heat total it is no longer part of.
    int64_t cop_heat_ws;
    int64_t cop_elec_ws;

    int64_t  flow_sum_c100, return_sum_c100, outdoor_sum_c100;
    uint32_t flow_n, return_n, outdoor_n;
    bool     has_flow_at_stop;
    int32_t  flow_at_stop_c100;

    // Seconds until the next run starts; 0 at a changeover, -1 for the last run.
    int32_t off_after_s;
} cycle_run_t;

typedef struct {
    cycle_options_t opt;
    uint32_t        period_start;
    uint32_t        period_end;

    // Finished runs, in the caller's array.
    cycle_run_t *runs;
    size_t       max_runs;
    size_t       count;
    bool         truncated; // more runs than max_runs; the rest were not kept

    // The compressor run in progress, as one segment per mode it has been in.
    bool        in_run;
    bool        run_from_first_sample;
    bool        run_mode_recorded;
    cycle_run_t segs[CYCLE_MAX_SEGMENTS];
    size_t      seg_count;

    // Samples since the electrical input last dropped below the threshold. They join the run if
    // it picks up again within min_gap_s, and are let go if it does not.
    bool        in_gap;
    cycle_run_t gap;

    bool     has_prev;
    uint32_t prev_end_ts;
    uint32_t prev_step_s;

    // Over the whole period, runs or not.
    int64_t  covered_s;
    int64_t  total_elec_ws;
    int64_t  outdoor_sum_c100;
    uint32_t outdoor_n;
    bool     mode_recorded;
} cycle_state_t;

typedef struct {
    double   period_hours;
    // Share of the period with an electrical reading. Figures from a period with gaps describe
    // only the part that was recorded.
    double   data_coverage_pct;
    bool     mode_recorded;
    uint32_t runs, heating_runs, dhw_runs;
    // Compressor starts into heating per recorded hour. Changeovers from hot water are not starts.
    bool     has_heating_starts_per_hour;
    double   heating_starts_per_hour;
    bool     has_median_run, has_median_heating_run, has_median_off;
    double   median_run_min, median_heating_run_min, median_off_min;
    bool     has_cop_all, has_cop_heating, has_cop_dhw;
    double   cop_all, cop_heating, cop_dhw;
    double   heat_kwh, elec_kwh;
    double   heating_heat_kwh, heating_elec_kwh;
    double   dhw_heat_kwh, dhw_elec_kwh;
    // Electricity recorded while no run was in progress: standby, pumps, controls.
    double   elec_kwh_outside_runs;
    bool     has_mean_outdoor;
    double   mean_outdoor_c;
    uint32_t excluded_heat_samples;
    bool     truncated;
} cycle_summary_t;

// Whether a heat reading is believable alongside the electricity drawn at the same instant.
bool cycle_heat_is_plausible(int32_t heat_w, bool has_elec, int32_t elec_w, uint32_t rated_output_w);

// `runs` is the caller's, and must outlive the state. period_start/end bound the samples that
// will be fed, and are what decides whether a run was clipped.
void cycle_init(cycle_state_t *state, const cycle_options_t *opt, uint32_t period_start,
                uint32_t period_end, cycle_run_t *runs, size_t max_runs);

void cycle_add_sample(cycle_state_t *state, const cycle_sample_t *sample);

// Closes a run still in progress and fills in each run's off time. Call once, after the last
// sample.
void cycle_finish(cycle_state_t *state);

// False if the working memory for the medians could not be allocated.
bool cycle_summarise(const cycle_state_t *state, cycle_summary_t *out);

const char *cycle_mode_name(cycle_mode_t mode);

#ifdef __cplusplus
}
#endif
