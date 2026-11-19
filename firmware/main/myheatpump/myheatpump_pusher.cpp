#include "myheatpump_pusher.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "mcp/mcp_directory.h"
#include "storage/history_logger.h"
#include "storage/time_sync.h"

static const char *TAG = "myheatpump";

#define NVS_NAMESPACE "myheatpump"

// The TLS handshake, for an https:// URL, runs on this task's stack.
#define TASK_STACK    8192
#define TASK_PRIORITY 3

#define HTTP_TIMEOUT_MS 5000

// How long to wait for the HTTP task to copy the room and emitter settings. If it is busy -- a
// test post to emoncms can hold it for many seconds -- the copy it made last time is used.
#define DIRECTORY_WAIT_MS 2000

// The size of the chunks the body is written to the socket in. This, and not the number of rooms,
// bounds the memory the payload needs.
#define WRITE_CHUNK 256

#define NOTIFY_PUSH   (1u << 0)
#define NOTIFY_CONFIG (1u << 1)

// ---------------------------------------------------------------------------
// The payload
// ---------------------------------------------------------------------------
//
// {"timestamp":"2026-11-18T09:00:00Z",
//  "system":{"heat_output_w":3600,"electrical_input_w":960,"flow_temp_c":38.4,"return_temp_c":33.6,
//            "outdoor_temp_c":4.2,"indoor_temp_c":19.6,"flow_rate_lph":640,"voltage_v":241,
//            "heating_running":true,"dhw_running":false},
//  "rooms":[{"id":101,"name":"Hall","temp_c":19.2,"target_temp_c":19,
//            "predicted_heat_loss_w_per_c":30,
//            "radiators":[{"id":201,"name":"Hall radiator","rated_output_w_dt50":1000,
//                          "flow_temp_c":37.6,"return_temp_c":33.2}],
//            "ufh_shares":[{"manifold_id":301,"pct":70}]}],
//  "ufh_manifolds":[{"id":301,"name":"Ground floor UFH","rated_output_w":2500,
//                    "flow_temp_c":33.8,"return_temp_c":30.1}]}
//
// A reading with no value is null, never a placeholder zero. There is no "floor": the board does
// not know which storey a room is on. A UFH manifold listed among a room's radiators is left out
// of "radiators"; it is reported under "ufh_manifolds" and reached through "ufh_shares".
//
// Everything is copied into one snapshot first and the body is then written from that, twice:
// once to count its length for Content-Length and once to the socket, in WRITE_CHUNK pieces. The
// snapshot does not change between the two, so the count is exact.

struct room_entry_t
{
    mcp_room_info_t info;
    bool has_temp;
    int16_t temp_c100;
};

struct rad_entry_t
{
    mcp_radiator_info_t info;
    bool has_flow;
    bool has_return;
    int16_t flow_c100;
    int16_t return_c100;
};

struct snapshot_t
{
    uint32_t ts;

    bool has_heat;
    int64_t heat_mw;
    bool has_elec;
    int64_t elec_mw;
    bool has_flow_temp;
    int32_t flow_temp_c100;
    bool has_return_temp;
    int32_t return_temp_c100;
    bool has_outdoor;
    int16_t outdoor_c100;
    bool has_indoor;
    int16_t indoor_c100;
    bool has_flow_rate;
    uint16_t flow_rate_lph;
    bool has_voltage;
    int64_t voltage_mv;
    bool has_heating;
    bool heating;
    bool has_dhw;
    bool dhw;

    size_t room_count;
    room_entry_t rooms[HISTORY_MAX_ROOMS];
    size_t rad_count;
    rad_entry_t rads[HISTORY_MAX_RADIATORS];
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static home_manager_t *s_home_manager;
static httpd_handle_t (*s_get_server)(void);
static TaskHandle_t s_task;
static esp_timer_handle_t s_timer;

// Guards s_config and s_status, which the HTTP task reads and writes.
static SemaphoreHandle_t s_mutex;
static myheatpump_config_t s_config;
static myheatpump_status_t s_status;

// The room and emitter settings as the HTTP task last copied them, guarded by s_dir_mutex.
// s_dir_done is given each time a copy finishes.
static SemaphoreHandle_t s_dir_mutex;
static SemaphoreHandle_t s_dir_done;
static mcp_room_info_t *s_dir_rooms;
static size_t s_dir_room_count;
static mcp_radiator_info_t *s_dir_rads;
static size_t s_dir_rad_count;

// Only the pusher task touches this. In PSRAM: about 8 KB with the caps at their maximum.
static snapshot_t *s_snap;

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

static void load_config(myheatpump_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->interval_s = MYHEATPUMP_INTERVAL_DEFAULT_S;

    nvs_handle_t nvs;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
    {
        return;
    }

    uint8_t enabled = 0;
    if (nvs_get_u8(nvs, "enabled", &enabled) == ESP_OK)
    {
        c->enabled = enabled != 0;
    }

    size_t len = sizeof(c->base_url);
    nvs_get_str(nvs, "base_url", c->base_url, &len);

    len = sizeof(c->api_key);
    nvs_get_str(nvs, "api_key", c->api_key, &len);

    uint16_t interval = 0;
    if (nvs_get_u16(nvs, "interval_s", &interval) == ESP_OK && interval >= MYHEATPUMP_INTERVAL_MIN_S &&
        interval <= MYHEATPUMP_INTERVAL_MAX_S)
    {
        c->interval_s = interval;
    }

    nvs_close(nvs);
}

static esp_err_t save_config(const myheatpump_config_t *c)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_u8(nvs, "enabled", c->enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(nvs, "base_url", c->base_url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "api_key", c->api_key);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "interval_s", c->interval_s);
    if (err == ESP_OK) err = nvs_commit(nvs);

    nvs_close(nvs);

    return err;
}

// ---------------------------------------------------------------------------
// Taking the snapshot
// ---------------------------------------------------------------------------

// Runs on the HTTP task, the one task that may walk the manager lists for mcp_directory.
static void copy_directory(void *arg)
{
    xSemaphoreTake(s_dir_mutex, portMAX_DELAY);
    s_dir_room_count = mcp_directory_rooms(s_dir_rooms, HISTORY_MAX_ROOMS);
    s_dir_rad_count = mcp_directory_radiators(s_dir_rads, HISTORY_MAX_RADIATORS);
    xSemaphoreGive(s_dir_mutex);

    xSemaphoreGive(s_dir_done);
}

static void refresh_directory(void)
{
    httpd_handle_t server = s_get_server ? s_get_server() : NULL;

    if (!server)
    {
        return;
    }

    // Clear a completion left over from a copy that finished after we stopped waiting for it.
    xSemaphoreTake(s_dir_done, 0);

    if (httpd_queue_work(server, copy_directory, NULL) == ESP_OK &&
        xSemaphoreTake(s_dir_done, pdMS_TO_TICKS(DIRECTORY_WAIT_MS)) != pdTRUE)
    {
        ESP_LOGW(TAG, "The HTTP task is busy; using the room settings copied last time");
    }
}

static void take_snapshot(snapshot_t *s)
{
    const home_manager_t *h = s_home_manager;

    s->ts = (uint32_t)time(NULL);

    // Each value is tested and then copied once, unlocked, for the reason history_logger's
    // build_record() gives: these fields are written from the CHIP event loop.
    s->has_heat = h->has_heat_meter_power;
    s->heat_mw = h->heat_meter_power_mw;
    s->has_elec = h->has_electrical_power;
    s->elec_mw = h->electrical_power_mw;
    s->has_flow_temp = h->has_heat_meter_flow_temperature;
    s->flow_temp_c100 = h->heat_meter_flow_temperature;
    s->has_return_temp = h->has_heat_meter_return_temperature;
    s->return_temp_c100 = h->heat_meter_return_temperature;
    s->has_outdoor = h->has_outdoor_temperature;
    s->outdoor_c100 = h->outdoor_temperature;
    s->has_indoor = h->has_average_internal_temperature;
    s->indoor_c100 = h->average_internal_temperature;
    s->has_flow_rate = h->has_heat_meter_flow;
    s->flow_rate_lph = h->heat_meter_flow;
    s->has_voltage = h->has_electrical_voltage;
    s->voltage_mv = h->electrical_voltage_mv;
    s->has_heating = h->has_heating_running;
    s->heating = h->heating_running;
    s->has_dhw = h->has_dhw_running;
    s->dhw = h->dhw_running;

    refresh_directory();

    xSemaphoreTake(s_dir_mutex, portMAX_DELAY);

    s->room_count = s_dir_room_count;
    for (size_t i = 0; i < s->room_count; i++)
    {
        s->rooms[i].info = s_dir_rooms[i];
    }

    s->rad_count = s_dir_rad_count;
    for (size_t i = 0; i < s->rad_count; i++)
    {
        s->rads[i].info = s_dir_rads[i];
    }

    xSemaphoreGive(s_dir_mutex);

    for (size_t i = 0; i < s->room_count; i++)
    {
        room_entry_t *r = &s->rooms[i];
        history_logger_room_reading(r->info.id, &r->has_temp, &r->temp_c100);
    }

    for (size_t i = 0; i < s->rad_count; i++)
    {
        rad_entry_t *r = &s->rads[i];
        history_logger_radiator_reading(r->info.id, &r->has_flow, &r->flow_c100, &r->has_return, &r->return_c100);
    }
}

static const rad_entry_t *find_rad(const snapshot_t *s, uint8_t id)
{
    for (size_t i = 0; i < s->rad_count; i++)
    {
        if (s->rads[i].info.id == id)
        {
            return &s->rads[i];
        }
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Writing JSON
// ---------------------------------------------------------------------------

// Counts what it is given, and with a client also sends it in WRITE_CHUNK pieces.
struct writer_t
{
    esp_http_client_handle_t client; // NULL while only counting
    size_t total;
    size_t used;
    bool failed;
    char buf[WRITE_CHUNK];
};

static void w_flush(writer_t *w)
{
    if (w->client && w->used && !w->failed)
    {
        int n = esp_http_client_write(w->client, w->buf, (int)w->used);

        if (n != (int)w->used)
        {
            w->failed = true;
        }
    }

    w->used = 0;
}

static void w_raw(writer_t *w, const char *s, size_t len)
{
    w->total += len;

    if (!w->client)
    {
        return;
    }

    while (len > 0)
    {
        size_t take = sizeof(w->buf) - w->used;
        take = take < len ? take : len;

        memcpy(w->buf + w->used, s, take);
        w->used += take;
        s += take;
        len -= take;

        if (w->used == sizeof(w->buf))
        {
            w_flush(w);
        }
    }
}

static void w_str(writer_t *w, const char *s)
{
    w_raw(w, s, strlen(s));
}

static void w_fmt(writer_t *w, const char *fmt, ...)
{
    char tmp[64];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    if (n > 0)
    {
        w_raw(w, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
    }
}

// A JSON string, escaped. NULL or empty is written as null.
static void w_jstr(writer_t *w, const char *s)
{
    if (!s || !s[0])
    {
        w_str(w, "null");
        return;
    }

    w_raw(w, "\"", 1);

    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\')
        {
            char esc[2] = {'\\', (char)c};
            w_raw(w, esc, 2);
        }
        else if (c < 0x20)
        {
            w_fmt(w, "\\u%04x", c);
        }
        else
        {
            w_raw(w, (const char *)&c, 1);
        }
    }

    w_raw(w, "\"", 1);
}

// "key": followed by the value, or null when there is none. `sep` is "" or ",".
static void w_temp(writer_t *w, const char *sep, const char *key, bool has, int32_t c100)
{
    if (has)
    {
        w_fmt(w, "%s\"%s\":%.2f", sep, key, (double)c100 / 100.0);
    }
    else
    {
        w_fmt(w, "%s\"%s\":null", sep, key);
    }
}

static void w_int(writer_t *w, const char *sep, const char *key, bool has, long long value)
{
    if (has)
    {
        w_fmt(w, "%s\"%s\":%lld", sep, key, value);
    }
    else
    {
        w_fmt(w, "%s\"%s\":null", sep, key);
    }
}

static void w_bool(writer_t *w, const char *sep, const char *key, bool has, bool value)
{
    w_fmt(w, "%s\"%s\":%s", sep, key, has ? (value ? "true" : "false") : "null");
}

// mW (or mV) to the nearest whole W (or V), rounding halves away from zero.
static long long milli_to_unit(int64_t milli)
{
    return (long long)(milli >= 0 ? (milli + 500) / 1000 : (milli - 500) / 1000);
}

static void write_payload(writer_t *w, const snapshot_t *s)
{
    char iso[24];
    time_t ts = (time_t)s->ts;
    struct tm tm;

    gmtime_r(&ts, &tm);
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm);

    w_fmt(w, "{\"timestamp\":\"%s\",\"system\":{", iso);
    w_int(w, "", "heat_output_w", s->has_heat, milli_to_unit(s->heat_mw));
    w_int(w, ",", "electrical_input_w", s->has_elec, milli_to_unit(s->elec_mw));
    w_temp(w, ",", "flow_temp_c", s->has_flow_temp, s->flow_temp_c100);
    w_temp(w, ",", "return_temp_c", s->has_return_temp, s->return_temp_c100);
    w_temp(w, ",", "outdoor_temp_c", s->has_outdoor, s->outdoor_c100);
    w_temp(w, ",", "indoor_temp_c", s->has_indoor, s->indoor_c100);
    w_int(w, ",", "flow_rate_lph", s->has_flow_rate, s->flow_rate_lph);
    w_int(w, ",", "voltage_v", s->has_voltage, milli_to_unit(s->voltage_mv));
    w_bool(w, ",", "heating_running", s->has_heating, s->heating);
    w_bool(w, ",", "dhw_running", s->has_dhw, s->dhw);
    w_str(w, "},\"rooms\":[");

    for (size_t i = 0; i < s->room_count; i++)
    {
        const room_entry_t *r = &s->rooms[i];

        w_fmt(w, "%s{\"id\":%u,\"name\":", i ? "," : "", r->info.id);
        w_jstr(w, r->info.name);
        w_temp(w, ",", "temp_c", r->has_temp, r->temp_c100);
        w_temp(w, ",", "target_temp_c", true, r->info.target_temp_c100);
        w_int(w, ",", "predicted_heat_loss_w_per_c", true, r->info.heat_loss_w_per_c);
        w_str(w, ",\"radiators\":[");

        bool first = true;

        for (uint8_t k = 0; k < r->info.radiator_count; k++)
        {
            uint8_t id = r->info.radiators[k];
            const rad_entry_t *rad = find_rad(s, id);

            if (rad && rad->info.is_ufh_manifold)
            {
                continue;
            }

            w_fmt(w, "%s{\"id\":%u,\"name\":", first ? "" : ",", id);
            w_jstr(w, rad ? rad->info.name : NULL);
            w_int(w, ",", "rated_output_w_dt50", rad != NULL, rad ? rad->info.rated_output_w : 0);
            w_temp(w, ",", "flow_temp_c", rad && rad->has_flow, rad ? rad->flow_c100 : 0);
            w_temp(w, ",", "return_temp_c", rad && rad->has_return, rad ? rad->return_c100 : 0);
            w_str(w, "}");
            first = false;
        }

        w_str(w, "],\"ufh_shares\":[");

        for (uint8_t k = 0; k < r->info.ufh_share_count; k++)
        {
            w_fmt(w, "%s{\"manifold_id\":%u,\"pct\":%u}", k ? "," : "", r->info.ufh_shares[k].manifold_id,
                  r->info.ufh_shares[k].pct);
        }

        w_str(w, "]}");
    }

    w_str(w, "],\"ufh_manifolds\":[");

    bool first = true;

    for (size_t i = 0; i < s->rad_count; i++)
    {
        const rad_entry_t *m = &s->rads[i];

        if (!m->info.is_ufh_manifold)
        {
            continue;
        }

        w_fmt(w, "%s{\"id\":%u,\"name\":", first ? "" : ",", m->info.id);
        w_jstr(w, m->info.name);
        w_int(w, ",", "rated_output_w", true, m->info.rated_output_w);
        w_temp(w, ",", "flow_temp_c", m->has_flow, m->flow_c100);
        w_temp(w, ",", "return_temp_c", m->has_return, m->return_c100);
        w_str(w, "}");
        first = false;
    }

    w_str(w, "]}");
}

// ---------------------------------------------------------------------------
// Posting
// ---------------------------------------------------------------------------

static void record(bool ok, int http_status, const char *error, size_t bytes)
{
    uint32_t now = (uint32_t)time(NULL);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_status.last_payload_bytes = (uint32_t)bytes;

    if (ok)
    {
        s_status.posts_ok++;
        s_status.last_success_ts = now;
    }
    else
    {
        s_status.posts_failed++;
        s_status.last_error_ts = now;
        s_status.last_error_http_status = http_status;
        strlcpy(s_status.last_error, error, sizeof(s_status.last_error));
    }

    xSemaphoreGive(s_mutex);

    if (!ok)
    {
        ESP_LOGW(TAG, "Post failed (HTTP %d): %s", http_status, error);
    }
}

static void push_once(const myheatpump_config_t *config)
{
    // Before the first SNTP sync the timestamp would be in 1970.
    if (!clock_is_valid())
    {
        ESP_LOGD(TAG, "Clock not set yet; not pushing");
        return;
    }

    take_snapshot(s_snap);

    // First pass: count.
    static writer_t w;
    memset(&w, 0, sizeof(w));
    write_payload(&w, s_snap);
    size_t length = w.total;

    static char url[MYHEATPUMP_URL_MAX + 16];
    size_t base_len = strlen(config->base_url);

    while (base_len > 0 && config->base_url[base_len - 1] == '/')
    {
        base_len--;
    }

    snprintf(url, sizeof(url), "%.*s/api/status", (int)base_len, config->base_url);

    esp_http_client_config_t http = {};
    http.url = url;
    http.method = HTTP_METHOD_POST;
    http.timeout_ms = HTTP_TIMEOUT_MS;
    http.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&http);

    if (!client)
    {
        record(false, 0, "Could not create the HTTP client", length);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (config->api_key[0])
    {
        esp_http_client_set_header(client, "X-Api-Key", config->api_key);
    }

    esp_err_t err = esp_http_client_open(client, (int)length);

    if (err != ESP_OK)
    {
        record(false, 0, esp_err_to_name(err), length);
        esp_http_client_cleanup(client);
        return;
    }

    // Second pass: send.
    memset(&w, 0, sizeof(w));
    w.client = client;
    write_payload(&w, s_snap);
    w_flush(&w);

    if (w.failed || w.total != length)
    {
        record(false, 0, "Could not send the body", length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }

    if (esp_http_client_fetch_headers(client) < 0)
    {
        record(false, 0, "No response", length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }

    int status = esp_http_client_get_status_code(client);

    // The start of the body, to explain a rejection. A 202 has nothing worth keeping.
    char body[MYHEATPUMP_RESPONSE_MAX] = "";
    int n = esp_http_client_read(client, body, sizeof(body) - 1);
    body[n > 0 ? n : 0] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status >= 200 && status < 300)
    {
        record(true, status, "", length);
        ESP_LOGD(TAG, "Pushed %u bytes, HTTP %d", (unsigned)length, status);
    }
    else
    {
        record(false, status, status == 401 ? "The server rejected the API key" : body, length);
    }
}

// ---------------------------------------------------------------------------
// The task
// ---------------------------------------------------------------------------

static void timer_cb(void *arg)
{
    xTaskNotify(s_task, NOTIFY_PUSH, eSetBits);
}

static bool can_post(const myheatpump_config_t *config)
{
    return config->enabled && config->base_url[0] != '\0';
}

static void apply_config(const myheatpump_config_t *config)
{
    esp_timer_stop(s_timer);

    if (can_post(config))
    {
        esp_timer_start_periodic(s_timer, (uint64_t)config->interval_s * 1000000ULL);
    }

    ESP_LOGI(TAG, "%s, every %u s", can_post(config) ? "Pushing" : "Not pushing", config->interval_s);
}

static void pusher_task(void *arg)
{
    myheatpump_config_t config;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    config = s_config;
    xSemaphoreGive(s_mutex);

    apply_config(&config);

    for (;;)
    {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);

        if (bits & NOTIFY_CONFIG)
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            config = s_config;
            xSemaphoreGive(s_mutex);

            apply_config(&config);
        }

        // A failure is recorded and left: the next attempt is the next tick, with fresh readings.
        // skip_unhandled_events on the timer means a slow post never queues up a burst behind it.
        if ((bits & NOTIFY_PUSH) && can_post(&config))
        {
            push_once(&config);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t myheatpump_pusher_init(home_manager_t *home_manager, httpd_handle_t (*get_server)(void))
{
    if (s_task)
    {
        return ESP_OK;
    }

    s_home_manager = home_manager;
    s_get_server = get_server;

    s_mutex = xSemaphoreCreateMutex();
    s_dir_mutex = xSemaphoreCreateMutex();
    s_dir_done = xSemaphoreCreateBinary();
    s_dir_rooms = (mcp_room_info_t *)heap_caps_calloc(HISTORY_MAX_ROOMS, sizeof(mcp_room_info_t),
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_dir_rads = (mcp_radiator_info_t *)heap_caps_calloc(HISTORY_MAX_RADIATORS, sizeof(mcp_radiator_info_t),
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_snap = (snapshot_t *)heap_caps_calloc(1, sizeof(snapshot_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!s_mutex || !s_dir_mutex || !s_dir_done || !s_dir_rooms || !s_dir_rads || !s_snap)
    {
        ESP_LOGE(TAG, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    load_config(&s_config);

    const esp_timer_create_args_t timer_args = {
        .callback = timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "myheatpump",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_timer);

    if (err != ESP_OK)
    {
        return err;
    }

    if (xTaskCreate(pusher_task, "myheatpump", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS)
    {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void myheatpump_pusher_get_config(myheatpump_config_t *config, bool *key_set)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *config = s_config;
    xSemaphoreGive(s_mutex);

    if (key_set)
    {
        *key_set = config->api_key[0] != '\0';
    }

    memset(config->api_key, 0, sizeof(config->api_key));
}

esp_err_t myheatpump_pusher_set_config(const myheatpump_config_t *config, bool clear_key)
{
    bool empty = config->base_url[0] == '\0';
    bool http = strncmp(config->base_url, "http://", 7) == 0;
    bool https = strncmp(config->base_url, "https://", 8) == 0;

    if ((!empty && !http && !https) || config->interval_s < MYHEATPUMP_INTERVAL_MIN_S ||
        config->interval_s > MYHEATPUMP_INTERVAL_MAX_S)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    myheatpump_config_t updated = *config;

    if (clear_key)
    {
        memset(updated.api_key, 0, sizeof(updated.api_key));
    }
    else if (updated.api_key[0] == '\0')
    {
        strlcpy(updated.api_key, s_config.api_key, sizeof(updated.api_key));
    }

    esp_err_t err = save_config(&updated);

    if (err == ESP_OK)
    {
        s_config = updated;
    }

    xSemaphoreGive(s_mutex);

    if (err == ESP_OK && s_task)
    {
        xTaskNotify(s_task, NOTIFY_CONFIG, eSetBits);
    }

    return err;
}

void myheatpump_pusher_get_status(myheatpump_status_t *status)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_mutex);
}
