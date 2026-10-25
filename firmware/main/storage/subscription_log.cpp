#include "subscription_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <lib/core/CHIPError.h>
#include <lib/core/ErrorStr.h>

#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"

static const char *TAG = "subscription_log";

#define SUBLOG_DIR SD_CARD_MOUNT_POINT "/subs"

// Events waiting for the next flush. A node failing every RETRY_DELAY_MS (30 s) adds two a minute,
// so this is a long way past what one flush sees; it only fills if the clock never gets set, and
// then the oldest go first.
#define RING_CAPACITY 256

typedef struct {
    int64_t  uptime_us;
    uint64_t node_id;
    uint32_t ts; // 0 until the clock was valid; the flush works it out from uptime_us
    uint32_t error;
    uint8_t  event;
} pending_event_t;

static SemaphoreHandle_t s_mutex;
static pending_event_t   s_ring[RING_CAPACITY];
static size_t            s_head;  // index of the oldest
static size_t            s_count;
static uint32_t          s_dropped;

// Only the flush task touches these.
static pending_event_t       s_batch[RING_CAPACITY];
static std::vector<uint64_t> s_up_nodes; // subscribed as of the last event written

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

esp_err_t subscription_log_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    subscription_log_record(0, SUBLOG_BOOT, 0);
    return ESP_OK;
}

void subscription_log_record(uint64_t node_id, sublog_event_t event, uint32_t error)
{
    if (!s_mutex) {
        return;
    }

    pending_event_t ev = {};
    ev.uptime_us = esp_timer_get_time();
    ev.node_id   = node_id;
    ev.ts        = clock_is_valid() ? (uint32_t)time(NULL) : 0;
    ev.error     = error;
    ev.event     = (uint8_t)event;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_count == RING_CAPACITY) {
        s_head = (s_head + 1) % RING_CAPACITY;
        s_count--;
        s_dropped++;
    }

    s_ring[(s_head + s_count) % RING_CAPACITY] = ev;
    s_count++;

    xSemaphoreGive(s_mutex);
}

// The wall time of an event taken before the clock was set, from how long ago it happened.
static uint32_t wall_time_of(const pending_event_t &ev, uint32_t now, int64_t uptime_now)
{
    if (ev.ts != 0) {
        return ev.ts;
    }
    return now - (uint32_t)((uptime_now - ev.uptime_us) / 1000000);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

static void apply_to_up_nodes(uint8_t event, uint64_t node_id)
{
    auto it = std::find(s_up_nodes.begin(), s_up_nodes.end(), node_id);

    switch (event) {
    case SUBLOG_BOOT:
        s_up_nodes.clear();
        break;
    case SUBLOG_ESTABLISHED:
        if (it == s_up_nodes.end()) {
            s_up_nodes.push_back(node_id);
        }
        break;
    case SUBLOG_TERMINATED:
    case SUBLOG_ATTEMPT_FAILED:
    case SUBLOG_REMOVED:
        if (it != s_up_nodes.end()) {
            s_up_nodes.erase(it);
        }
        break;
    default:
        break;
    }
}

static bool path_for_day(uint32_t ts, char *out, size_t len)
{
    char date[16];
    local_date_string(ts, date, sizeof(date));
    return snprintf(out, len, "%s/%s", SUBLOG_DIR, date) < (int)len;
}

static bool write_record(FILE *f, uint64_t node_id, uint32_t ts, uint8_t event, uint32_t error)
{
    sublog_record_t rec = {};
    rec.node_id = node_id;
    rec.ts      = ts;
    rec.error   = error;
    rec.event   = event;
    return fwrite(&rec, sizeof(rec), 1, f) == 1;
}

// Opens the file for the local day containing ts for appending, creating it -- header, DAY_START
// and the nodes subscribed at that moment -- if this is its first record.
static FILE *open_day(uint32_t ts)
{
    char path[HISTORY_PATH_MAX];
    if (!path_for_day(ts, path, sizeof(path))) {
        return NULL;
    }

    FILE *f = fopen(path, "r+b");
    if (f) {
        sublog_header_t hdr;
        if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != SUBLOG_MAGIC ||
            hdr.record_size != sizeof(sublog_record_t)) {
            ESP_LOGE(TAG, "Bad header in %s, skipping", path);
            fclose(f);
            return NULL;
        }
        fseek(f, 0, SEEK_END);
        return f;
    }

    // EEXIST on every day but the first; fopen reports any real failure.
    mkdir(SUBLOG_DIR, 0777);

    f = fopen(path, "w+b");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s", path);
        return NULL;
    }

    sublog_header_t hdr = {SUBLOG_MAGIC, SUBLOG_VERSION, sizeof(sublog_record_t)};
    uint32_t        day = local_midnight_for(ts);
    bool            ok  = fwrite(&hdr, sizeof(hdr), 1, f) == 1 && write_record(f, 0, day, SUBLOG_DAY_START, 0);

    for (size_t i = 0; ok && i < s_up_nodes.size(); i++) {
        ok = write_record(f, s_up_nodes[i], day, SUBLOG_UP_AT_START, 0);
    }

    if (!ok) {
        ESP_LOGE(TAG, "Cannot start %s", path);
        fclose(f);
        return NULL;
    }

    return f;
}

void subscription_log_flush(void)
{
    if (!s_mutex || !clock_is_valid()) {
        return; // held until the events can be given a date
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    size_t   count   = s_count;
    uint32_t dropped = s_dropped;
    for (size_t i = 0; i < count; i++) {
        s_batch[i] = s_ring[(s_head + i) % RING_CAPACITY];
    }
    s_head    = 0;
    s_count   = 0;
    s_dropped = 0;

    xSemaphoreGive(s_mutex);

    if (dropped > 0) {
        ESP_LOGW(TAG, "%lu subscription events dropped before they could be written", (unsigned long)dropped);
    }

    uint32_t now        = (uint32_t)time(NULL);
    int64_t  uptime_now = esp_timer_get_time();
    bool     card       = sd_card_available();

    FILE    *f        = NULL;
    uint32_t open_for = 0; // local midnight of the day f is open on

    for (size_t i = 0; i < count; i++) {
        const pending_event_t &ev = s_batch[i];
        uint32_t               ts = wall_time_of(ev, now, uptime_now);

        if (card) {
            uint32_t day = local_midnight_for(ts);
            if (!f || day != open_for) {
                if (f) {
                    fclose(f);
                }
                f        = open_day(ts);
                open_for = day;
            }
            if (f && !write_record(f, ev.node_id, ts, ev.event, ev.error)) {
                ESP_LOGE(TAG, "Short write of a subscription event");
            }
        }

        // After the write, so a day file opened for this event starts from the state before it.
        apply_to_up_nodes(ev.event, ev.node_id);
    }

    if (card) {
        uint32_t today = local_midnight_for(now);
        if (!f || today != open_for) {
            if (f) {
                fclose(f);
            }
            f = open_day(now);
        }
        if (f) {
            write_record(f, 0, now, SUBLOG_ALIVE, 0);
        }
    }

    if (f) {
        fclose(f);
    }
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

static const char *event_name(uint8_t event)
{
    switch (event) {
    case SUBLOG_BOOT:           return "boot";
    case SUBLOG_DAY_START:      return "dayStart";
    case SUBLOG_UP_AT_START:    return "upAtStart";
    case SUBLOG_ESTABLISHED:    return "established";
    case SUBLOG_TERMINATED:     return "terminated";
    case SUBLOG_ATTEMPT_FAILED: return "failed";
    case SUBLOG_REMOVED:        return "removed";
    default:                    return NULL;
    }
}

// Streams a day's records as JSON objects. ALIVE records are not sent: they only say the monitor
// was running, so each boot carries the last one before it as lastAlive instead, and the latest is
// the response's own lastAlive.
typedef struct {
    httpd_req_t *req;
    bool         first;
    uint32_t     last_alive; // 0 when none yet
} emitter_t;

static esp_err_t emit(emitter_t *em, const sublog_record_t &rec)
{
    if (rec.event == SUBLOG_ALIVE) {
        em->last_alive = rec.ts;
        return ESP_OK;
    }

    const char *name = event_name(rec.event);
    if (!name) {
        return ESP_OK; // written by a later firmware
    }

    char buf[384];
    int  n = snprintf(buf, sizeof(buf), "%s{\"ts\":%lu,\"event\":\"%s\"", em->first ? "" : ",",
                      (unsigned long)rec.ts, name);

    if (rec.event == SUBLOG_BOOT) {
        n += em->last_alive ? snprintf(buf + n, sizeof(buf) - n, ",\"lastAlive\":%lu", (unsigned long)em->last_alive)
                            : snprintf(buf + n, sizeof(buf) - n, ",\"lastAlive\":null");
    } else if (rec.event != SUBLOG_DAY_START) {
        n += snprintf(buf + n, sizeof(buf) - n, ",\"nodeId\":%llu", (unsigned long long)rec.node_id);
    }

    if (rec.event == SUBLOG_ATTEMPT_FAILED && rec.error != 0) {
        chip::ErrorStrStorage storage;
        const char           *text = chip::ErrorStr(CHIP_ERROR(rec.error), false, storage);

        n += snprintf(buf + n, sizeof(buf) - n, ",\"code\":%lu,\"error\":\"", (unsigned long)rec.error);
        // The text is CHIP's own, but escape it anyway rather than trust it to hold no quote.
        for (const char *p = text; *p && n < (int)sizeof(buf) - 8; p++) {
            if (*p == '"' || *p == '\\') {
                buf[n++] = '\\';
            }
            buf[n++] = (*p >= 0x20) ? *p : ' ';
        }
        n += snprintf(buf + n, sizeof(buf) - n, "\"");
    }

    snprintf(buf + n, sizeof(buf) - n, "}");
    em->first = false;
    return httpd_resp_send_chunk(em->req, buf, HTTPD_RESP_USE_STRLEN);
}

esp_err_t subscription_log_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char date[16] = {0};
    char raw[32];
    size_t qlen = httpd_req_get_url_query_len(req);
    char   query[64];
    if (qlen > 0 && qlen < sizeof(query) && httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "date", raw, sizeof(raw)) == ESP_OK) {
        int y, m, d;
        if (strlen(raw) != 10 || sscanf(raw, "%4d-%2d-%2d", &y, &m, &d) != 3) {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "{\"error\":\"bad date\"}");
        }
        snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    } else if (clock_is_valid()) {
        local_date_string((uint32_t)time(NULL), date, sizeof(date));
    } else {
        return httpd_resp_sendstr(req, "{\"storage\":\"ok\",\"events\":[],\"lastAlive\":null}");
    }

    char path[HISTORY_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", SUBLOG_DIR, date);

    char head[96];
    snprintf(head, sizeof(head), "{\"storage\":\"ok\",\"date\":\"%s\",\"now\":%lu,\"events\":[", date,
             (unsigned long)time(NULL));
    httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

    emitter_t em = {req, true, 0};
    esp_err_t err = ESP_OK;

    FILE *f = fopen(path, "rb");
    if (f) {
        sublog_header_t hdr;
        if (fread(&hdr, sizeof(hdr), 1, f) == 1 && hdr.magic == SUBLOG_MAGIC &&
            hdr.record_size == sizeof(sublog_record_t)) {
            sublog_record_t chunk[32];
            size_t          got;
            while (err == ESP_OK && (got = fread(chunk, sizeof(chunk[0]), 32, f)) > 0) {
                for (size_t i = 0; i < got && err == ESP_OK; i++) {
                    err = emit(&em, chunk[i]);
                }
            }
        }
        fclose(f);
    }

    // Events from the last minute are still in RAM. Those on this day are appended, so today's
    // view is current rather than up to a flush behind.
    if (err == ESP_OK && clock_is_valid() && s_mutex) {
        pending_event_t *pending = (pending_event_t *)malloc(sizeof(pending_event_t) * RING_CAPACITY);
        size_t           count   = 0;

        if (pending) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            count = s_count;
            for (size_t i = 0; i < count; i++) {
                pending[i] = s_ring[(s_head + i) % RING_CAPACITY];
            }
            xSemaphoreGive(s_mutex);

            uint32_t now        = (uint32_t)time(NULL);
            int64_t  uptime_now = esp_timer_get_time();

            for (size_t i = 0; i < count && err == ESP_OK; i++) {
                sublog_record_t rec = {};
                rec.node_id = pending[i].node_id;
                rec.ts      = wall_time_of(pending[i], now, uptime_now);
                rec.error   = pending[i].error;
                rec.event   = pending[i].event;

                char day[16];
                local_date_string(rec.ts, day, sizeof(day));
                if (strcmp(day, date) == 0) {
                    err = emit(&em, rec);
                }
            }

            free(pending);
        }
    }

    if (err != ESP_OK) {
        return err; // the client has gone; nothing more can be sent
    }

    char tail[48];
    if (em.last_alive) {
        snprintf(tail, sizeof(tail), "],\"lastAlive\":%lu}", (unsigned long)em.last_alive);
    } else {
        snprintf(tail, sizeof(tail), "],\"lastAlive\":null}");
    }
    httpd_resp_send_chunk(req, tail, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}
