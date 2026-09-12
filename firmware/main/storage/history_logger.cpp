#include "history_logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "history_format.h"
#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"
#include "value_cache.h"

static const char *TAG = "history_logger";

// The flush does file I/O, so it gets its own task rather than running on the esp_timer
// task: that task's stack is CONFIG_ESP_TIMER_TASK_STACK_SIZE (3584 bytes here), which is
// not enough for FATFS, and a slow card would otherwise delay the sample timer too.
#define FLUSH_INTERVAL_MS   60000
#define FLUSH_TASK_STACK    4096
#define FLUSH_TASK_PRIORITY 3

// Matter cluster and attribute ids, kept local so this file stays free of the Matter
// SDK headers -- the same choice node_power_logger.cpp makes in
// matter-esp32-home-energy-manager. These match the ids decoded in app_main.cpp.
static constexpr uint32_t CLUSTER_TEMPERATURE = 0x0402;
static constexpr uint32_t CLUSTER_FLOW        = 0x0404;
static constexpr uint32_t CLUSTER_ELECTRICAL  = 0x0090;
static constexpr uint32_t CLUSTER_HEAT_METER  = 0xFFF1FC01;

static constexpr uint32_t ATTR_MEASURED_VALUE = 0x0000;
static constexpr uint32_t ATTR_EPM_VOLTAGE    = 0x0004;
static constexpr uint32_t ATTR_EPM_CURRENT    = 0x0005;
static constexpr uint32_t ATTR_EPM_POWER      = 0x0008;
static constexpr uint32_t ATTR_HM_FLOW        = 0x0000;
static constexpr uint32_t ATTR_HM_FLOW_TEMP   = 0x0001;
static constexpr uint32_t ATTR_HM_RETURN_TEMP = 0x0002;
static constexpr uint32_t ATTR_HM_POWER       = 0x0003;

// Matter device type ids, from node_manager.h and heat_meter_cluster.h.
static constexpr uint32_t DEVTYPE_TEMPERATURE = 770;
static constexpr uint32_t DEVTYPE_FLOW        = 774;
static constexpr uint32_t DEVTYPE_ELECTRICAL  = 1296;
static constexpr uint32_t DEVTYPE_HEAT_METER  = 0xFFF10001u;

// Cap on buffered samples between flushes. At the 5 s default a flush carries 12 per
// series; this leaves room for a fast interval across many sensors and still bounds the
// memory if a flush ever fails.
#define MAX_PENDING 4096

struct series_t {
    uint8_t  kind;
    uint64_t node_id;      // 0 for the synthetic home series
    uint16_t endpoint_id;
};

struct pending_t {
    series_t series;
    uint32_t base_ts;      // local midnight of the day this sample belongs to
    uint32_t slot;
    uint8_t  data[HISTORY_MAX_RECORD_SIZE];
};

static SemaphoreHandle_t      s_mutex;
static std::vector<series_t>  s_series;
static std::vector<pending_t> s_pending;
static esp_timer_handle_t     s_sample_timer;
static TaskHandle_t           s_flush_task;

static node_manager_t *s_node_manager;
static home_manager_t *s_home_manager;

// s_interval_s is what the current day's files are being written at; s_pending_interval_s
// is what the user has asked for. They are latched together at the local-midnight rollover
// so that a file's records are always spaced at the interval its header records -- which is
// the invariant that makes offset = header + slot * record_size correct.
static uint16_t s_interval_s         = HISTORY_INTERVAL_DEFAULT_S;
static uint16_t s_pending_interval_s = HISTORY_INTERVAL_DEFAULT_S;
static uint32_t s_active_day_base;
static bool     s_restart_sampler;
static uint32_t s_dropped;

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------

// Narrows to int16 with the sentinel reserved. Anything at or beyond the sentinel is
// clamped to INT16_MIN + 1 so a real reading can never be mistaken for "no reading".
static int16_t clamp_i16(int64_t v)
{
    if (v > INT16_MAX)     return INT16_MAX;
    if (v <= HIST_NULL_I16) return HIST_NULL_I16 + 1;
    return (int16_t)v;
}

static uint16_t clamp_u16(int64_t v)
{
    if (v < 0)              return 0;
    if (v >= HIST_NULL_U16) return HIST_NULL_U16 - 1;
    return (uint16_t)v;
}

// Reads one cached attribute, returning the sentinel when it has never been reported.
static int16_t cached_i16(const series_t &s, uint32_t cluster, uint32_t attr, int64_t divisor)
{
    int64_t raw = 0;
    if (!ValueCache::instance().get(s.node_id, s.endpoint_id, cluster, attr, &raw)) {
        return HIST_NULL_I16;
    }
    return clamp_i16(divisor > 1 ? raw / divisor : raw);
}

static uint16_t cached_u16(const series_t &s, uint32_t cluster, uint32_t attr, int64_t divisor)
{
    int64_t raw = 0;
    if (!ValueCache::instance().get(s.node_id, s.endpoint_id, cluster, attr, &raw)) {
        return HIST_NULL_U16;
    }
    return clamp_u16(divisor > 1 ? raw / divisor : raw);
}

// Builds the record for a series. Returns the record size, or 0 if the kind is unknown.
static size_t build_record(const series_t &s, uint8_t *out)
{
    switch (s.kind) {
    case KIND_TEMPERATURE: {
        rec_temperature_t r;
        r.temp_c100 = cached_i16(s, CLUSTER_TEMPERATURE, ATTR_MEASURED_VALUE, 1);
        memcpy(out, &r, sizeof(r));
        return sizeof(r);
    }
    case KIND_FLOW: {
        rec_flow_t r;
        r.flow_m3h10 = cached_u16(s, CLUSTER_FLOW, ATTR_MEASURED_VALUE, 1);
        memcpy(out, &r, sizeof(r));
        return sizeof(r);
    }
    case KIND_ELECTRICAL: {
        rec_electrical_t r;
        r.power_w    = cached_i16(s, CLUSTER_ELECTRICAL, ATTR_EPM_POWER,   1000); // mW -> W
        r.voltage_dv = cached_u16(s, CLUSTER_ELECTRICAL, ATTR_EPM_VOLTAGE,  100); // mV -> 0.1 V
        r.current_ca = cached_u16(s, CLUSTER_ELECTRICAL, ATTR_EPM_CURRENT,   10); // mA -> 0.01 A
        memcpy(out, &r, sizeof(r));
        return sizeof(r);
    }
    case KIND_HEAT_METER: {
        rec_heat_meter_t r;
        r.power_w          = cached_i16(s, CLUSTER_HEAT_METER, ATTR_HM_POWER, 1000); // mW -> W
        r.flow_temp_c100   = cached_i16(s, CLUSTER_HEAT_METER, ATTR_HM_FLOW_TEMP,   1);
        r.return_temp_c100 = cached_i16(s, CLUSTER_HEAT_METER, ATTR_HM_RETURN_TEMP, 1);
        // app_main.cpp caches the flow in l/h, the unit the cluster reports and the unit this
        // record stores, so it crosses unscaled.
        r.flow_lph         = cached_u16(s, CLUSTER_HEAT_METER, ATTR_HM_FLOW, 1);
        memcpy(out, &r, sizeof(r));
        return sizeof(r);
    }
    // case KIND_HOME: {
    //     const home_manager_t *h = s_home_manager;
    //     rec_home_t r;

    //     // A quantity with no sensor bound to it is genuinely absent, not zero, so it gets
    //     // the sentinel. Where a sensor IS bound, zero is a real reading and is kept.
    //     r.flow_temp_c100    = h->heat_meter_flow_temp_node_id   ? h->heat_source_flow_temperature   : HIST_NULL_I16;
    //     r.return_temp_c100  = h->heat_meter_return_temp_node_id ? h->heat_source_return_temperature : HIST_NULL_I16;
    //     r.outdoor_temp_c100 = h->outdoor_temp_node_id            ? h->outdoor_temperature            : HIST_NULL_I16;
    //     r.flow_m3h10        = h->heat_source_flow_rate_node_id   ? h->heat_source_flow_rate          : HIST_NULL_U16;

    //     r.elec_power_w = h->has_electrical_power ? clamp_i16(h->electrical_power_mw / 1000) : HIST_NULL_I16;

    //     // A heat meter is authoritative when selected; otherwise the output is derived from
    //     // flow rate and delta-T, which needs all three sensors bound to mean anything.
    //     if (h->heat_meter_node_id) {
    //         r.heat_output_w = h->has_heat_meter_power ? clamp_i16(h->heat_meter_power_mw / 1000) : HIST_NULL_I16;
    //         r.flow_temp_c100   = h->has_heat_meter_flow_temp   ? clamp_i16(h->heat_meter_flow_temp)   : HIST_NULL_I16;
    //         r.return_temp_c100 = h->has_heat_meter_return_temp ? clamp_i16(h->heat_meter_return_temp) : HIST_NULL_I16;
    //         r.flow_m3h10       = h->has_heat_meter_flow ? clamp_u16((int64_t)(h->heat_meter_flow_m3h * 10.0f)) : HIST_NULL_U16;
    //     } else if (h->heat_source_flow_rate_node_id && h->heat_meter_flow_temp_node_id &&
    //                h->heat_meter_return_temp_node_id) {
    //         r.heat_output_w = clamp_i16(h->heat_source_output);
    //     } else {
    //         r.heat_output_w = HIST_NULL_I16;
    //     }

    //     memcpy(out, &r, sizeof(r));
    //     return sizeof(r);
    // }
    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Series discovery
// ---------------------------------------------------------------------------

static uint8_t kind_for_device_type(uint32_t device_type_id)
{
    switch (device_type_id) {
    case DEVTYPE_TEMPERATURE: return KIND_TEMPERATURE;
    case DEVTYPE_FLOW:        return KIND_FLOW;
    case DEVTYPE_ELECTRICAL:  return KIND_ELECTRICAL;
    case DEVTYPE_HEAT_METER:  return KIND_HEAT_METER;
    default:                  return 0;
    }
}

// Rebuilds the sampled set from the node manager: every endpoint whose device type we
// know how to record, plus the synthetic home series. Called at init and once per flush,
// so commissioning or deleting a device is picked up without walking the node list on
// every sample.
static void refresh_series(void)
{
    std::vector<series_t> next;

    next.push_back(series_t{KIND_HOME, 0, 0});

    for (matter_node_t *node = s_node_manager->node_list; node; node = node->next) {
        for (uint16_t i = 0; i < node->endpoints_count; i++) {
            const endpoint_entry_t *ep = &node->endpoints[i];

            for (uint8_t d = 0; d < ep->device_type_count; d++) {
                uint8_t kind = kind_for_device_type(ep->device_type_ids[d]);
                if (!kind) {
                    continue;
                }
                next.push_back(series_t{kind, node->node_id, ep->endpoint_id});
                break; // one series per endpoint, first recognised device type wins
            }
        }
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_series = std::move(next);
    size_t count = s_series.size();
    xSemaphoreGive(s_mutex);

    ESP_LOGD(TAG, "Tracking %u series", (unsigned)count);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

static void on_sample_timer(void *arg)
{
    // Before SNTP syncs, time(NULL) is somewhere in 1970 and every dated filename would
    // be wrong, so there is nothing useful to record yet.
    if (!sd_card_available() || !clock_is_valid()) {
        return;
    }

    uint32_t now     = (uint32_t)time(nullptr);
    uint32_t base_ts = local_midnight_for(now);

    // A new local day starts new files, which is the only point at which the interval can
    // change without breaking the slot arithmetic of a file already part-written.
    if (base_ts != s_active_day_base) {
        s_active_day_base = base_ts;
        if (s_interval_s != s_pending_interval_s) {
            s_interval_s      = s_pending_interval_s;
            s_restart_sampler = true; // the flush task re-arms the timer; not safe from here
        }
    }

    uint16_t interval = s_interval_s;
    uint32_t slot     = (now - base_ts) / interval;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_pending.size() >= MAX_PENDING) {
        s_dropped++;
        xSemaphoreGive(s_mutex);
        return;
    }

    for (const series_t &s : s_series) {
        pending_t p;
        p.series  = s;
        p.base_ts = base_ts;
        p.slot    = slot;
        memset(p.data, 0, sizeof(p.data));

        if (build_record(s, p.data) == 0) {
            continue;
        }
        s_pending.push_back(p);
    }

    xSemaphoreGive(s_mutex);
}

// Appends one series' records for one day, padding any slots that were missed so that
// offset = header + slot * record_size stays true.
static void write_series_day(const series_t &series, uint32_t base_ts,
                             const std::vector<pending_t *> &records)
{
    size_t rec_size = history_record_size(series.kind);
    if (rec_size == 0) {
        return;
    }

    char path[HISTORY_PATH_MAX];
    if (!history_path_for(series.kind, series.node_id, series.endpoint_id, base_ts,
                          path, sizeof(path))) {
        return;
    }

    FILE *f = fopen(path, "r+b");
    if (!f) {
        // New day, or first ever record for this series.
        f = fopen(path, "w+b");
        if (!f) {
            ESP_LOGW(TAG, "Cannot open %s", path);
            return;
        }
        history_header_t hdr;
        hdr.magic       = HISTORY_MAGIC;
        hdr.version     = HISTORY_VERSION;
        hdr.sensor_kind = series.kind;
        hdr.interval_s  = s_interval_s;
        hdr.base_ts     = base_ts;
        hdr.record_size = (uint16_t)rec_size;
        hdr.reserved    = 0;
        if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
            ESP_LOGE(TAG, "Cannot write header to %s", path);
            fclose(f);
            return;
        }
    }

    // The interval in the header is what this file was written at. If the user changed it
    // mid-day, keep writing at the old cadence rather than corrupting the slot arithmetic;
    // the new interval takes effect with tomorrow's file.
    history_header_t hdr;
    fseek(f, 0, SEEK_SET);
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != HISTORY_MAGIC ||
        hdr.record_size != rec_size) {
        ESP_LOGE(TAG, "Bad header in %s, skipping", path);
        fclose(f);
        return;
    }

    fseek(f, 0, SEEK_END);
    long   end        = ftell(f);
    size_t next_slot  = (end > (long)sizeof(hdr)) ? ((size_t)end - sizeof(hdr)) / rec_size : 0;

    uint8_t sentinel[HISTORY_MAX_RECORD_SIZE];
    history_fill_sentinel(series.kind, sentinel);

    for (pending_t *p : records) {
        if (p->slot < next_slot) {
            continue; // already written; a clock step backwards is the only way here
        }
        while (next_slot < p->slot) {
            if (fwrite(sentinel, rec_size, 1, f) != 1) {
                ESP_LOGE(TAG, "Short write padding %s", path);
                fclose(f);
                return;
            }
            next_slot++;
        }
        if (fwrite(p->data, rec_size, 1, f) != 1) {
            ESP_LOGE(TAG, "Short write to %s", path);
            fclose(f);
            return;
        }
        next_slot++;
    }

    fclose(f);
}

static void flush_once(void)
{
    std::vector<pending_t> batch;
    uint32_t               dropped;

    // Drain under the lock, write outside it: the sample timer must never wait on the card.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    batch.swap(s_pending);
    dropped   = s_dropped;
    s_dropped = 0;
    xSemaphoreGive(s_mutex);

    if (dropped) {
        ESP_LOGW(TAG, "Dropped %lu samples: buffer full", (unsigned long)dropped);
    }

    if (!batch.empty() && sd_card_available()) {
        // Group by (series, day). A batch spans two days only across local midnight.
        std::vector<bool> done(batch.size(), false);

        for (size_t i = 0; i < batch.size(); i++) {
            if (done[i]) {
                continue;
            }
            std::vector<pending_t *> group;
            for (size_t j = i; j < batch.size(); j++) {
                if (done[j]) {
                    continue;
                }
                if (batch[j].series.kind == batch[i].series.kind &&
                    batch[j].series.node_id == batch[i].series.node_id &&
                    batch[j].series.endpoint_id == batch[i].series.endpoint_id &&
                    batch[j].base_ts == batch[i].base_ts) {
                    done[j] = true;
                    group.push_back(&batch[j]);
                }
            }
            std::sort(group.begin(), group.end(),
                      [](const pending_t *a, const pending_t *b) { return a->slot < b->slot; });
            write_series_day(batch[i].series, batch[i].base_ts, group);
        }
    }

    refresh_series();

    // Re-arming the sample timer is done here rather than from inside its own callback.
    if (s_restart_sampler) {
        s_restart_sampler = false;
        esp_timer_stop(s_sample_timer);
        esp_timer_start_periodic(s_sample_timer, (uint64_t)s_interval_s * 1000000ULL);
        ESP_LOGI(TAG, "Sampling interval now %u s", s_interval_s);
    }
}

static void flush_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(FLUSH_INTERVAL_MS));
        flush_once();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void history_logger_set_interval(uint16_t interval_s)
{
    if (interval_s < HISTORY_INTERVAL_MIN_S) interval_s = HISTORY_INTERVAL_MIN_S;
    if (interval_s > HISTORY_INTERVAL_MAX_S) interval_s = HISTORY_INTERVAL_MAX_S;

    if (interval_s == s_pending_interval_s) {
        return;
    }
    s_pending_interval_s = interval_s;

    // Before the logger is running there is no part-written file to protect, so the change
    // applies at once; afterwards it waits for the next local midnight.
    if (!s_sample_timer) {
        s_interval_s = interval_s;
        return;
    }

    ESP_LOGI(TAG, "Sampling interval will change to %u s at midnight (currently %u s)",
             interval_s, s_interval_s);
}

uint16_t history_logger_interval(void)
{
    return s_pending_interval_s;
}

uint16_t history_logger_active_interval(void)
{
    return s_interval_s;
}

void history_logger_forget_node(uint64_t node_id)
{
    ValueCache::instance().forget_node(node_id);
    if (s_mutex) {
        refresh_series();
    }
}

esp_err_t history_logger_init(node_manager_t *node_manager, home_manager_t *home_manager)
{
    s_node_manager = node_manager;
    s_home_manager = home_manager;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    if (home_manager->logging_interval_s) {
        history_logger_set_interval(home_manager->logging_interval_s);
    }

    refresh_series();

    esp_timer_create_args_t sample_args = {};
    sample_args.callback = on_sample_timer;
    sample_args.name     = "history_sample";
    esp_err_t err = esp_timer_create(&sample_args, &s_sample_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_start_periodic(s_sample_timer, (uint64_t)s_interval_s * 1000000ULL);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(flush_task, "history_flush", FLUSH_TASK_STACK, NULL,
                    FLUSH_TASK_PRIORITY, &s_flush_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "History logger started: %u s sampling, %u ms flush", s_interval_s,
             FLUSH_INTERVAL_MS);
    return ESP_OK;
}
