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

static const char *TAG = "history_logger";

// The flush does file I/O, so it gets its own task rather than running on the esp_timer
// task: that task's stack is CONFIG_ESP_TIMER_TASK_STACK_SIZE (3584 bytes here), which is
// not enough for FATFS, and a slow card would otherwise delay the sample timer too.
#define FLUSH_INTERVAL_MS   60000
#define FLUSH_TASK_STACK    4096
#define FLUSH_TASK_PRIORITY 3

// Cap on buffered samples between flushes. At the 5 s default a flush carries 12; this
// leaves ample room for a fast interval and still bounds the memory if a flush ever fails.
#define MAX_PENDING 4096

struct pending_t {
    uint8_t  kind;
    uint32_t base_ts;      // local midnight of the day this sample belongs to
    uint32_t slot;
    uint8_t  data[HISTORY_MAX_RECORD_SIZE];
};

static SemaphoreHandle_t      s_mutex;
static std::vector<pending_t> s_pending;
static esp_timer_handle_t     s_sample_timer;
static TaskHandle_t           s_flush_task;

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

// Builds the home record from the manager's current values. Returns the record size.
//
// Reading home_manager directly rather than polling a cache means the sampler races
// attribute_data_cb, which writes these fields from the CHIP event loop: the 64-bit
// readings can in principle be read torn on a 32-bit core. That is left unlocked
// deliberately -- the cost is at worst a single clamped outlier sample every few weeks,
// against holding a lock that the Matter event loop would then contend on. What the code
// must not do is read a field twice, hence the locals below: the has_* test and the value
// it guards come from the same read.
static size_t build_record(uint8_t *out)
{
    const home_manager_t *h = s_home_manager;

    // Start from "no reading" for every field, including the reserved tail, so anything not
    // explicitly filled below reads back as null rather than as a plausible zero.
    rec_home_t r;
    history_fill_sentinel(KIND_HOME, &r);

    if (h->has_heat_meter_power) {
        int64_t mw = h->heat_meter_power_mw;
        r.heat_power_w = clamp_i16(mw / 1000);
    }

    if (h->has_electrical_power) {
        int64_t mw = h->electrical_power_mw;
        r.elec_power_w = clamp_i16(mw / 1000);
    }

    if (h->has_heat_meter_flow_temperature) {
        r.flow_temp_c100 = clamp_i16(h->heat_meter_flow_temperature);
    }

    if (h->has_heat_meter_return_temperature) {
        r.return_temp_c100 = clamp_i16(h->heat_meter_return_temperature);
    }

    // The cluster reports l/h, which is the unit this field stores, so it crosses unscaled.
    if (h->has_heat_meter_flow) {
        r.flow_lph = clamp_u16(h->heat_meter_flow);
    }

    if (h->has_outdoor_temperature) {
        r.outdoor_temp_c100 = clamp_i16(h->outdoor_temperature);
    }

    if (h->has_internal_temperature) {
        r.internal_temp_c100 = clamp_i16(h->internal_temperature);
    }

    if (h->has_cop) {
        r.cop_x100 = clamp_i16(h->cop_x100);
    }

    if (h->has_dhw_running) {
        r.dhw_running = h->dhw_running ? 1 : 0;
    }

    if (h->has_electrical_voltage) {
        int64_t mv = h->electrical_voltage_mv;
        r.elec_voltage_dv = clamp_u16(mv / 100);
    }

    if (h->has_electrical_current) {
        int64_t ma = h->electrical_current_ma;
        r.elec_current_ca = clamp_u16(ma / 10);
    }

    memcpy(out, &r, sizeof(r));
    return sizeof(r);
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

    pending_t p;
    p.kind    = KIND_HOME;
    p.base_ts = base_ts;
    p.slot    = slot;
    memset(p.data, 0, sizeof(p.data));

    if (build_record(p.data) == 0) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_pending.size() >= MAX_PENDING) {
        s_dropped++;
    } else {
        s_pending.push_back(p);
    }

    xSemaphoreGive(s_mutex);
}

// Appends one series' records for one day, padding any slots that were missed so that
// offset = header + slot * record_size stays true.
static void write_series_day(uint8_t kind, uint32_t base_ts,
                             const std::vector<pending_t *> &records)
{
    size_t rec_size = history_record_size(kind);
    if (rec_size == 0) {
        return;
    }

    char path[HISTORY_PATH_MAX];
    if (!history_path_for(kind, base_ts, path, sizeof(path))) {
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
        hdr.sensor_kind = kind;
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
    history_fill_sentinel(kind, sentinel);

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
        // Group by (kind, day). With one series this only ever splits across local midnight,
        // but the grouping is kept general so a second series costs nothing to add.
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
                if (batch[j].kind == batch[i].kind && batch[j].base_ts == batch[i].base_ts) {
                    done[j] = true;
                    group.push_back(&batch[j]);
                }
            }
            std::sort(group.begin(), group.end(),
                      [](const pending_t *a, const pending_t *b) { return a->slot < b->slot; });
            write_series_day(batch[i].kind, batch[i].base_ts, group);
        }
    }

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

esp_err_t history_logger_init(home_manager_t *home_manager)
{
    s_home_manager = home_manager;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    if (home_manager->logging_interval_s) {
        history_logger_set_interval(home_manager->logging_interval_s);
    }

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
