#include "mcp_cycles.h"

#include <stdlib.h>
#include <string.h>

bool cycle_heat_is_plausible(int32_t heat_w, bool has_elec, int32_t elec_w, uint32_t rated_output_w)
{
    if ((int64_t)heat_w > (int64_t)CYCLE_RATED_OUTPUT_MULTIPLE * rated_output_w) {
        return false;
    }

    // Heat with nothing going in is an infinite COP, so it fails the same test.
    if (has_elec && heat_w > 0) {
        int64_t elec = elec_w > 0 ? elec_w : 0;
        if ((int64_t)heat_w > CYCLE_MAX_PLAUSIBLE_COP * elec) {
            return false;
        }
    }
    return true;
}

const char *cycle_mode_name(cycle_mode_t mode)
{
    switch (mode) {
    case CYCLE_MODE_HEATING: return "heating";
    case CYCLE_MODE_DHW:     return "dhw";
    case CYCLE_MODE_OTHER:   return "other";
    default:                 return "unknown";
    }
}

void cycle_init(cycle_state_t *state, const cycle_options_t *opt, uint32_t period_start,
                uint32_t period_end, cycle_run_t *runs, size_t max_runs)
{
    memset(state, 0, sizeof(*state));
    state->opt          = *opt;
    state->period_start = period_start;
    state->period_end   = period_end;
    state->runs         = runs;
    state->max_runs     = max_runs;
}

static void seg_begin(cycle_run_t *seg, uint32_t ts)
{
    memset(seg, 0, sizeof(*seg));
    seg->start_ts    = ts;
    seg->end_ts      = ts;
    seg->off_after_s = -1;
}

static void seg_add(cycle_run_t *seg, const cycle_sample_t *s, uint32_t rated_output_w)
{
    seg->end_ts = s->ts + s->step_s;

    if (s->has_elec) {
        seg->elec_sum_w += s->elec_w;
        seg->elec_n++;
        seg->elec_ws += (int64_t)s->elec_w * s->step_s;
    }

    if (s->has_heat) {
        if (cycle_heat_is_plausible(s->heat_w, s->has_elec, s->elec_w, rated_output_w)) {
            if (seg->heat_n == 0 || s->heat_w > seg->max_heat_w) {
                seg->max_heat_w = s->heat_w;
            }
            seg->heat_sum_w += s->heat_w;
            seg->heat_n++;
            seg->heat_ws += (int64_t)s->heat_w * s->step_s;

            if (s->has_elec) {
                seg->cop_heat_ws += (int64_t)s->heat_w * s->step_s;
                seg->cop_elec_ws += (int64_t)s->elec_w * s->step_s;
            }
        } else {
            seg->excluded++;
        }
    }

    if (s->has_flow) {
        seg->flow_sum_c100 += s->flow_c100;
        seg->flow_n++;
        seg->has_flow_at_stop  = true;
        seg->flow_at_stop_c100 = s->flow_c100;
    }
    if (s->has_return) {
        seg->return_sum_c100 += s->return_c100;
        seg->return_n++;
    }
    if (s->has_outdoor) {
        seg->outdoor_sum_c100 += s->outdoor_c100;
        seg->outdoor_n++;
    }
}

// Adds `from` to `into`. The two are adjacent in time, in either order.
static void seg_merge(cycle_run_t *into, const cycle_run_t *from)
{
    bool from_is_later = from->end_ts >= into->end_ts;

    if (from->heat_n && (into->heat_n == 0 || from->max_heat_w > into->max_heat_w)) {
        into->max_heat_w = from->max_heat_w;
    }
    if (from->has_flow_at_stop && (from_is_later || !into->has_flow_at_stop)) {
        into->has_flow_at_stop  = true;
        into->flow_at_stop_c100 = from->flow_at_stop_c100;
    }

    if (from->start_ts < into->start_ts) {
        into->start_ts = from->start_ts;
    }
    if (from->end_ts > into->end_ts) {
        into->end_ts = from->end_ts;
    }

    into->heat_sum_w       += from->heat_sum_w;
    into->heat_n           += from->heat_n;
    into->heat_ws          += from->heat_ws;
    into->excluded         += from->excluded;
    into->elec_sum_w       += from->elec_sum_w;
    into->elec_n           += from->elec_n;
    into->elec_ws          += from->elec_ws;
    into->cop_heat_ws      += from->cop_heat_ws;
    into->cop_elec_ws      += from->cop_elec_ws;
    into->flow_sum_c100    += from->flow_sum_c100;
    into->flow_n           += from->flow_n;
    into->return_sum_c100  += from->return_sum_c100;
    into->return_n         += from->return_n;
    into->outdoor_sum_c100 += from->outdoor_sum_c100;
    into->outdoor_n        += from->outdoor_n;
}

static void segs_remove(cycle_state_t *state, size_t index)
{
    memmove(&state->segs[index], &state->segs[index + 1],
            (state->seg_count - index - 1) * sizeof(cycle_run_t));
    state->seg_count--;
}

// Ends the compressor run in progress and files its segments as runs. `still_on` is true when it
// is being ended by the data running out rather than by the compressor stopping.
static void finalize_run(cycle_state_t *state, bool still_on)
{
    state->in_run = false;
    state->in_gap = false; // the samples since it stopped were never part of it

    cycle_run_t *segs  = state->segs;
    uint32_t     start = segs[0].start_ts;
    uint32_t     end   = segs[state->seg_count - 1].end_ts;

    if (end - start < state->opt.min_run_s) {
        return;
    }

    // A stretch in one mode shorter than min_run_s is folded into its neighbour, so a flag that
    // flickers does not shatter a run into fragments. Folding can leave two stretches of the same
    // mode side by side, which are then one.
    bool changed = true;
    while (changed && state->seg_count > 1) {
        changed = false;

        for (size_t i = 0; i < state->seg_count; i++) {
            if (segs[i].end_ts - segs[i].start_ts >= state->opt.min_run_s) {
                continue;
            }
            seg_merge(i > 0 ? &segs[i - 1] : &segs[i + 1], &segs[i]);
            segs_remove(state, i);
            changed = true;
            break;
        }

        for (size_t i = 1; i < state->seg_count;) {
            if (segs[i].mode == segs[i - 1].mode) {
                seg_merge(&segs[i - 1], &segs[i]);
                segs_remove(state, i);
            } else {
                i++;
            }
        }
    }

    for (size_t i = 0; i < state->seg_count; i++) {
        cycle_run_t *seg = &segs[i];

        // Nothing ever set a mode: the compressor ran with neither flag up, or they were not logged.
        if (seg->mode == CYCLE_MODE_NONE) {
            seg->mode = state->run_mode_recorded ? CYCLE_MODE_OTHER : CYCLE_MODE_UNKNOWN;
        }

        seg->compressor_start = (i == 0);
        seg->clipped =
            (i == 0 && state->run_from_first_sample) ||
            (i == state->seg_count - 1 && still_on);

        if (state->count < state->max_runs) {
            state->runs[state->count++] = *seg;
        } else {
            state->truncated = true;
        }
    }
}

static cycle_mode_t sample_mode(const cycle_sample_t *s)
{
    if (s->dhw == 1) {
        return CYCLE_MODE_DHW;
    }
    if (s->heating == 1) {
        return CYCLE_MODE_HEATING;
    }
    return CYCLE_MODE_NONE;
}

void cycle_add_sample(cycle_state_t *state, const cycle_sample_t *s)
{
    bool first = !state->has_prev;

    // A hole in the data ends a run the same as the compressor stopping: what happened in it is
    // not known.
    if (state->has_prev && s->ts > state->prev_end_ts && state->in_run) {
        finalize_run(state, false);
    }

    if (s->has_elec) {
        state->covered_s     += s->step_s;
        state->total_elec_ws += (int64_t)s->elec_w * s->step_s;
    }
    if (s->has_outdoor) {
        state->outdoor_sum_c100 += s->outdoor_c100;
        state->outdoor_n++;
    }
    if (s->heating >= 0 || s->dhw >= 0) {
        state->mode_recorded = true;
    }

    bool on = s->has_elec && s->elec_w >= (int32_t)state->opt.on_threshold_w;

    if (on) {
        if (!state->in_run) {
            state->in_run            = true;
            state->in_gap            = false;
            state->run_mode_recorded = false;
            state->seg_count         = 1;
            seg_begin(&state->segs[0], s->ts);

            // Running in the very first sample of the period means it started before it.
            state->run_from_first_sample = first && s->ts - state->period_start < s->step_s;
        } else if (state->in_gap) {
            // It picked up again soon enough: the pause is part of the run.
            seg_merge(&state->segs[state->seg_count - 1], &state->gap);
            state->in_gap = false;
        }

        if (s->heating >= 0 || s->dhw >= 0) {
            state->run_mode_recorded = true;
        }

        // A sample in neither mode takes the mode of the samples around it, because the flags
        // are thermostat demand and can drop a moment before the compressor does.
        cycle_run_t *last = &state->segs[state->seg_count - 1];
        cycle_mode_t mode = sample_mode(s);

        if (mode != CYCLE_MODE_NONE) {
            if (last->mode == CYCLE_MODE_NONE) {
                last->mode = mode;
            } else if (last->mode != mode && state->seg_count < CYCLE_MAX_SEGMENTS) {
                last = &state->segs[state->seg_count++];
                seg_begin(last, s->ts);
                last->mode = mode;
            }
        }

        seg_add(last, s, state->opt.rated_output_w);
    } else if (state->in_run) {
        if (!state->in_gap) {
            state->in_gap = true;
            seg_begin(&state->gap, s->ts);
        }
        seg_add(&state->gap, s, state->opt.rated_output_w);

        if (state->gap.end_ts - state->gap.start_ts >= state->opt.min_gap_s) {
            finalize_run(state, false);
        }
    }

    state->has_prev    = true;
    state->prev_end_ts = s->ts + s->step_s;
    state->prev_step_s = s->step_s;
}

void cycle_finish(cycle_state_t *state)
{
    if (state->in_run) {
        // Still drawing power at the last sample of the period, as opposed to having stopped a
        // moment before it.
        bool still_on = !state->in_gap && (state->period_end <= state->prev_end_ts ||
                                           state->period_end - state->prev_end_ts < state->prev_step_s);
        finalize_run(state, still_on);
    }

    for (size_t i = 0; i + 1 < state->count; i++) {
        state->runs[i].off_after_s = (int32_t)(state->runs[i + 1].start_ts - state->runs[i].end_ts);
    }
}

static int compare_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

// Median of `n` durations in seconds, as minutes. Sorts `values` in place.
static bool median_minutes(uint32_t *values, size_t n, double *out)
{
    if (n == 0) {
        return false;
    }
    qsort(values, n, sizeof(uint32_t), compare_u32);

    double seconds = (n % 2) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
    *out = seconds / 60.0;
    return true;
}

static bool cop_of(int64_t heat_ws, int64_t elec_ws, double *out)
{
    if (elec_ws <= 0) {
        return false;
    }
    *out = (double)heat_ws / (double)elec_ws;
    return true;
}

bool cycle_summarise(const cycle_state_t *state, cycle_summary_t *out)
{
    memset(out, 0, sizeof(*out));

    size_t    n       = state->count;
    uint32_t *scratch = malloc((n ? n : 1) * sizeof(uint32_t));
    if (!scratch) {
        return false;
    }

    int64_t all_heat = 0, all_elec = 0, all_cop_heat = 0, all_cop_elec = 0;
    int64_t h_heat = 0, h_elec = 0, h_cop_heat = 0, h_cop_elec = 0;
    int64_t d_heat = 0, d_elec = 0, d_cop_heat = 0, d_cop_elec = 0;
    uint32_t heating_starts = 0;

    for (size_t i = 0; i < n; i++) {
        const cycle_run_t *run = &state->runs[i];

        all_heat     += run->heat_ws;
        all_elec     += run->elec_ws;
        all_cop_heat += run->cop_heat_ws;
        all_cop_elec += run->cop_elec_ws;
        out->excluded_heat_samples += run->excluded;

        if (run->mode == CYCLE_MODE_HEATING) {
            out->heating_runs++;
            if (run->compressor_start) {
                heating_starts++;
            }
            h_heat     += run->heat_ws;
            h_elec     += run->elec_ws;
            h_cop_heat += run->cop_heat_ws;
            h_cop_elec += run->cop_elec_ws;
        } else if (run->mode == CYCLE_MODE_DHW) {
            out->dhw_runs++;
            d_heat     += run->heat_ws;
            d_elec     += run->elec_ws;
            d_cop_heat += run->cop_heat_ws;
            d_cop_elec += run->cop_elec_ws;
        }
    }

    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        scratch[m++] = state->runs[i].end_ts - state->runs[i].start_ts;
    }
    out->has_median_run = median_minutes(scratch, m, &out->median_run_min);

    m = 0;
    for (size_t i = 0; i < n; i++) {
        if (state->runs[i].mode == CYCLE_MODE_HEATING) {
            scratch[m++] = state->runs[i].end_ts - state->runs[i].start_ts;
        }
    }
    out->has_median_heating_run = median_minutes(scratch, m, &out->median_heating_run_min);

    // Changeovers have an off time of zero and are not off periods.
    m = 0;
    for (size_t i = 0; i < n; i++) {
        if (state->runs[i].off_after_s > 0) {
            scratch[m++] = (uint32_t)state->runs[i].off_after_s;
        }
    }
    out->has_median_off = median_minutes(scratch, m, &out->median_off_min);

    free(scratch);

    uint32_t period_s = state->period_end - state->period_start;

    out->period_hours      = period_s / 3600.0;
    out->data_coverage_pct = period_s ? (double)state->covered_s * 100.0 / period_s : 0.0;
    if (out->data_coverage_pct > 100.0) {
        out->data_coverage_pct = 100.0;
    }
    out->mode_recorded = state->mode_recorded;
    out->runs          = (uint32_t)n;
    out->truncated     = state->truncated;

    if (state->covered_s > 0) {
        out->has_heating_starts_per_hour = true;
        out->heating_starts_per_hour     = heating_starts / (state->covered_s / 3600.0);
    }

    out->has_cop_all     = cop_of(all_cop_heat, all_cop_elec, &out->cop_all);
    out->has_cop_heating = cop_of(h_cop_heat, h_cop_elec, &out->cop_heating);
    out->has_cop_dhw     = cop_of(d_cop_heat, d_cop_elec, &out->cop_dhw);

    out->heat_kwh              = all_heat / 3.6e6;
    out->elec_kwh              = all_elec / 3.6e6;
    out->heating_heat_kwh      = h_heat / 3.6e6;
    out->heating_elec_kwh      = h_elec / 3.6e6;
    out->dhw_heat_kwh          = d_heat / 3.6e6;
    out->dhw_elec_kwh          = d_elec / 3.6e6;
    out->elec_kwh_outside_runs = (state->total_elec_ws - all_elec) / 3.6e6;

    if (state->outdoor_n) {
        out->has_mean_outdoor = true;
        out->mean_outdoor_c   = (double)state->outdoor_sum_c100 / state->outdoor_n / 100.0;
    }

    return true;
}
