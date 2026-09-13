#include "history_api.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "history_format.h"
#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"

static const char *TAG = "history_api";

#define DEFAULT_POINTS 500
#define MAX_POINTS     2000

// Records read per disk pass. 256 x 28 bytes is around 7 KB -- a couple of sectors' worth
// of the widest record, and keeps this off the httpd task's stack. Scale this down if the
// record ever widens again: the buffer below is the product of the two.
#define READ_CHUNK_RECORDS 256

static uint8_t s_read_buf[READ_CHUNK_RECORDS * HISTORY_MAX_RECORD_SIZE];

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static esp_err_t send_json_chunk(httpd_req_t *req, const char *s)
{
    return httpd_resp_send_chunk(req, s, HTTPD_RESP_USE_STRLEN);
}

// One chunk per point would mean 800 TCP writes and 800 lots of chunked-encoding framing
// for a day view, so points are accumulated here and sent a kilobyte at a time.
typedef struct {
    httpd_req_t *req;
    char         buf[1024];
    size_t       len;
} json_out_t;

static void out_init(json_out_t *o, httpd_req_t *req)
{
    o->req = req;
    o->len = 0;
}

static void out_flush(json_out_t *o)
{
    if (o->len) {
        httpd_resp_send_chunk(o->req, o->buf, o->len);
        o->len = 0;
    }
}

static void out_write(json_out_t *o, const char *s, size_t n)
{
    if (o->len + n > sizeof(o->buf)) {
        out_flush(o);
    }
    if (n > sizeof(o->buf)) {
        httpd_resp_send_chunk(o->req, s, n); // larger than the buffer: pass it straight through
        return;
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
}

static void out_str(json_out_t *o, const char *s)
{
    out_write(o, s, strlen(s));
}

static esp_err_t send_unavailable(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"storage\":\"unavailable\"}");
    return ESP_OK;
}

// Pulls one query parameter out of the request. Returns false if absent.
static bool query_param(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1 || qlen > 256) {
        return false;
    }

    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

// Names every column, in record order, including the reserved tail. The UI charts by
// position and checks this list before it does, so a field added to the reserved tail shows
// up as a named column without the UI needing to be rebuilt.
static const char *field_names_for(uint8_t kind)
{
    switch (kind) {
    case KIND_HOME: return "\"heatPowerW\",\"elecPowerW\",\"flowTempC100\","
                           "\"returnTempC100\",\"flowLph\",\"outdoorTempC100\","
                           "\"internalTempC100\",\"copX100\",\"dhwRunning\","
                           "\"elecVoltageDv\",\"elecCurrentCa\","
                           "\"reserved0\",\"reserved1\",\"reserved2\"";
    default:        return "";
    }
}

// Reads a 16-bit field out of a record, sign-extending signed fields.
static int32_t field_value(uint8_t kind, size_t index, const uint8_t *rec)
{
    uint16_t raw;
    memcpy(&raw, rec + index * 2, 2);

    if ((history_unsigned_mask(kind) >> index) & 1u) {
        return (int32_t)raw;
    }
    return (int32_t)(int16_t)raw;
}

// ---------------------------------------------------------------------------
// Series streaming
// ---------------------------------------------------------------------------

// Streams one file as JSON: header metadata, downsampled points, and (for the kinds that
// carry power) integrated energy.
//
// Buckets average across `stride` slots, ignoring sentinel slots. A bucket whose slots
// are all sentinel emits null, so the chart draws a gap rather than a line across an
// outage. Energy integrates every slot, not the buckets, so the totals do not change
// with the requested resolution.
static esp_err_t stream_file(httpd_req_t *req, const char *path, int points)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"storage\":\"ok\",\"points\":[],\"reason\":\"no data for that day\"}");
        return ESP_OK;
    }

    history_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != HISTORY_MAGIC ||
        hdr.version != HISTORY_VERSION) {
        fclose(f);
        ESP_LOGW(TAG, "Bad or unknown header in %s", path);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"unreadable history file\"}");
        return ESP_OK;
    }

    size_t rec_size = hdr.record_size;
    size_t fields   = history_field_count(hdr.sensor_kind);

    if (rec_size == 0 || rec_size > HISTORY_MAX_RECORD_SIZE ||
        rec_size != history_record_size(hdr.sensor_kind)) {
        fclose(f);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"record size mismatch\"}");
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long   end         = ftell(f);
    size_t total_slots = (end > (long)sizeof(hdr)) ? ((size_t)end - sizeof(hdr)) / rec_size : 0;
    fseek(f, sizeof(hdr), SEEK_SET);

    size_t stride = 1;
    if (points > 0 && total_slots > (size_t)points) {
        stride = (total_slots + points - 1) / points;
    }

    httpd_resp_set_type(req, "application/json");

    // Must hold the whole preamble including every field name -- about 300 characters for
    // rec_home_t's fourteen. snprintf would truncate silently and emit malformed JSON, so
    // grow this alongside the record rather than trimming it to today's exact need.
    char head[512];
    snprintf(head, sizeof(head),
             "{\"storage\":\"ok\",\"kind\":%u,\"interval\":%u,\"baseTs\":%lu,"
             "\"slots\":%u,\"stride\":%u,\"fields\":[%s],\"points\":[",
             (unsigned)hdr.sensor_kind, (unsigned)hdr.interval_s,
             (unsigned long)hdr.base_ts, (unsigned)total_slots, (unsigned)stride,
             field_names_for(hdr.sensor_kind));

    json_out_t out;
    out_init(&out, req);
    out_str(&out, head);

    // Per-bucket accumulators, and per-field energy over every slot.
    int64_t  bucket_sum[HISTORY_MAX_RECORD_SIZE / 2] = {0};
    uint32_t bucket_n[HISTORY_MAX_RECORD_SIZE / 2]   = {0};
    int64_t  energy_sum[HISTORY_MAX_RECORD_SIZE / 2] = {0}; // watt-seconds

    size_t slot        = 0;
    size_t in_bucket   = 0;
    size_t bucket_base = 0;
    bool   first_point = true;
    char   point[256];

    while (slot < total_slots) {
        size_t want = total_slots - slot;
        if (want > READ_CHUNK_RECORDS) {
            want = READ_CHUNK_RECORDS;
        }

        size_t got = fread(s_read_buf, rec_size, want, f);
        if (got == 0) {
            break;
        }

        for (size_t i = 0; i < got; i++) {
            const uint8_t *rec = s_read_buf + i * rec_size;

            if (in_bucket == 0) {
                bucket_base = slot;
                memset(bucket_sum, 0, sizeof(bucket_sum));
                memset(bucket_n, 0, sizeof(bucket_n));
            }

            for (size_t fi = 0; fi < fields; fi++) {
                uint16_t raw;
                memcpy(&raw, rec + fi * 2, 2);
                if (history_field_is_null(hdr.sensor_kind, fi, raw)) {
                    continue;
                }
                int32_t v = field_value(hdr.sensor_kind, fi, rec);
                bucket_sum[fi] += v;
                bucket_n[fi]++;
                energy_sum[fi] += (int64_t)v * hdr.interval_s;
            }

            slot++;
            in_bucket++;

            if (in_bucket == stride || slot == total_slots) {
                size_t n = 0;
                int    w = snprintf(point, sizeof(point), "%s[%lu", first_point ? "" : ",",
                                    (unsigned long)(hdr.base_ts + bucket_base * hdr.interval_s));
                if (w > 0 && (size_t)w < sizeof(point)) {
                    n = (size_t)w;
                }

                // A timestamp plus fourteen fields is about 130 characters, so `point` has
                // headroom; the guard is here so a wider record can never overrun it.
                for (size_t fi = 0; fi < fields && n + 1 < sizeof(point); fi++) {
                    if (bucket_n[fi] == 0) {
                        w = snprintf(point + n, sizeof(point) - n, ",null");
                    } else {
                        w = snprintf(point + n, sizeof(point) - n, ",%lld",
                                     (long long)(bucket_sum[fi] / bucket_n[fi]));
                    }
                    if (w <= 0 || (size_t)w >= sizeof(point) - n) {
                        break;
                    }
                    n += (size_t)w;
                }

                if (n + 1 < sizeof(point)) {
                    point[n++] = ']';
                    point[n]   = '\0';
                }
                out_write(&out, point, n);

                first_point = false;
                in_bucket   = 0;
            }
        }
    }
    fclose(f);

    // Energy only means something for the power fields, which are 0 and 1 by construction --
    // see the field-order note in history_format.h. This COP is the day's integrated figure,
    // which is the honest headline number; the per-slot copX100 column is the instantaneous
    // one, and the two will not agree.
    char tail[224];
    if (hdr.sensor_kind == KIND_HOME) {
        double heat_wh = (double)energy_sum[0] / 3600.0;
        double elec_wh = (double)energy_sum[1] / 3600.0;
        if (elec_wh > 0.0) {
            snprintf(tail, sizeof(tail),
                     "],\"energyWh\":{\"heat\":%.1f,\"electrical\":%.1f},\"cop\":%.2f}",
                     heat_wh, elec_wh, heat_wh / elec_wh);
        } else {
            snprintf(tail, sizeof(tail),
                     "],\"energyWh\":{\"heat\":%.1f,\"electrical\":%.1f},\"cop\":null}",
                     heat_wh, elec_wh);
        }
    } else {
        snprintf(tail, sizeof(tail), "]}");
    }
    out_str(&out, tail);
    out_flush(&out);

    return httpd_resp_send_chunk(req, NULL, 0);
}

static int requested_points(httpd_req_t *req)
{
    char buf[16];
    if (!query_param(req, "points", buf, sizeof(buf))) {
        return DEFAULT_POINTS;
    }

    int p = atoi(buf);
    if (p <= 0)          return DEFAULT_POINTS;
    if (p > MAX_POINTS)  return MAX_POINTS;
    return p;
}

// Defaults to today when no date is given, so the dashboard's first load needs no
// round trip to find out what "today" is on the device.
static bool requested_date(httpd_req_t *req, char *out, size_t out_len)
{
    char raw[32];
    if (query_param(req, "date", raw, sizeof(raw))) {
        char safe[16];
        if (!history_sanitize_token(raw, safe, sizeof(safe))) {
            return false;
        }
        strlcpy(out, safe, out_len);
        return true;
    }

    if (!clock_is_valid()) {
        return false;
    }
    local_date_string((uint32_t)time(NULL), out, out_len);
    return true;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

// GET /api/history — the resolved home series for one day.
static esp_err_t history_get_handler(httpd_req_t *req)
{
    if (!sd_card_available()) {
        return send_unavailable(req);
    }

    char date[16];
    if (!requested_date(req, date, sizeof(date))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"bad or missing date\"}");
        return ESP_OK;
    }

    char path[HISTORY_PATH_MAX];
    if (!history_path_for_date(KIND_HOME, date, path, sizeof(path))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"bad date\"}");
        return ESP_OK;
    }

    return stream_file(req, path, requested_points(req));
}

// GET /api/history/dates — which local days have data on the card, so a date picker can
// tell an empty day from a day that was never recorded.
static esp_err_t dates_handler(httpd_req_t *req)
{
    DIR *dir = opendir(SD_CARD_MOUNT_POINT);
    if (!dir) {
        return send_unavailable(req);
    }

    // One file per day, so the names are already distinct. Bounded so a card full of files
    // cannot exhaust the httpd task.
    char   dates[64][16];
    size_t date_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && date_count < 64) {
        char date[16];
        if (sscanf(ent->d_name, "home-%15s", date) == 1) {
            strlcpy(dates[date_count], date, sizeof(dates[0]));
            date_count++;
        }
    }
    closedir(dir);

    httpd_resp_set_type(req, "application/json");
    send_json_chunk(req, "{\"storage\":\"ok\",\"dates\":[");

    char buf[128];
    for (size_t i = 0; i < date_count; i++) {
        snprintf(buf, sizeof(buf), "%s\"%s\"", i ? "," : "", dates[i]);
        send_json_chunk(req, buf);
    }
    send_json_chunk(req, "]}");

    return httpd_resp_send_chunk(req, NULL, 0);
}

// GET /api/history/* — routes to the sub-resources above.
static esp_err_t history_sub_handler(httpd_req_t *req)
{
    if (!sd_card_available()) {
        return send_unavailable(req);
    }

    if (strncmp(req->uri, "/api/history/dates", 18) == 0) {
        return dates_handler(req);
    }

    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "{\"error\":\"unknown history resource\"}");
    return ESP_OK;
}

esp_err_t history_api_register(httpd_handle_t server)
{
    const httpd_uri_t history_uri = {
        .uri      = "/api/history",
        .method   = HTTP_GET,
        .handler  = history_get_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t history_sub_uri = {
        .uri      = "/api/history/*",
        .method   = HTTP_GET,
        .handler  = history_sub_handler,
        .user_ctx = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(server, &history_uri);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_register_uri_handler(server, &history_sub_uri);
}
