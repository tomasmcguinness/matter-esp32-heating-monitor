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

// Records read per disk pass. 512 x 12 bytes is one 4 KB sector's worth of the widest
// record, and keeps this off the httpd task's stack.
#define READ_CHUNK_RECORDS 512

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

static const char *field_names_for(uint8_t kind)
{
    switch (kind) {
    case KIND_TEMPERATURE: return "\"tempC100\"";
    case KIND_FLOW:        return "\"flowM3h10\"";
    case KIND_ELECTRICAL:  return "\"powerW\",\"voltageDv\",\"currentCa\"";
    case KIND_HEAT_METER:  return "\"powerW\",\"flowTempC100\",\"returnTempC100\",\"flowLph\"";
    case KIND_HOME:        return "\"heatOutputW\",\"elecPowerW\",\"flowTempC100\","
                                  "\"returnTempC100\",\"flowM3h10\",\"outdoorTempC100\"";
    default:               return "";
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

    char head[320];
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
    char   point[192];

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

                // A timestamp plus six fields is about 60 characters, so `point` is never
                // close to full; the guard is here so a wider record can never overrun it.
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

    // Energy only means something for the power fields: field 0 of an electrical or heat
    // meter record, and fields 0 and 1 of a home record.
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
    } else if (hdr.sensor_kind == KIND_ELECTRICAL || hdr.sensor_kind == KIND_HEAT_METER) {
        snprintf(tail, sizeof(tail), "],\"energyWh\":{\"power\":%.1f}}",
                 (double)energy_sum[0] / 3600.0);
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
    if (!history_path_for_date(KIND_HOME, 0, 0, date, path, sizeof(path))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"bad date\"}");
        return ESP_OK;
    }

    return stream_file(req, path, requested_points(req));
}

// GET /api/history/sensors — what is on the card.
static esp_err_t sensors_handler(httpd_req_t *req)
{
    DIR *dir = opendir(SD_CARD_MOUNT_POINT);
    if (!dir) {
        return send_unavailable(req);
    }

    // Distinct sensors and distinct dates, both bounded so a card full of files cannot
    // exhaust the httpd task.
    struct { uint64_t node; uint16_t endpoint; } sensors[32];
    size_t sensor_count = 0;
    char   dates[64][16];
    size_t date_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;

        unsigned long long node;
        unsigned           endpoint;
        char               date[16];

        if (sscanf(name, "sensor-%llu-%u-%15s", &node, &endpoint, date) == 3) {
            bool seen = false;
            for (size_t i = 0; i < sensor_count; i++) {
                if (sensors[i].node == node && sensors[i].endpoint == endpoint) {
                    seen = true;
                    break;
                }
            }
            if (!seen && sensor_count < 32) {
                sensors[sensor_count].node     = node;
                sensors[sensor_count].endpoint = (uint16_t)endpoint;
                sensor_count++;
            }
        } else if (sscanf(name, "home-%15s", date) == 1) {
            bool seen = false;
            for (size_t i = 0; i < date_count; i++) {
                if (strcmp(dates[i], date) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen && date_count < 64) {
                strlcpy(dates[date_count], date, sizeof(dates[0]));
                date_count++;
            }
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

    send_json_chunk(req, "],\"sensors\":[");
    for (size_t i = 0; i < sensor_count; i++) {
        snprintf(buf, sizeof(buf), "%s{\"nodeId\":%llu,\"endpointId\":%u}", i ? "," : "",
                 (unsigned long long)sensors[i].node, (unsigned)sensors[i].endpoint);
        send_json_chunk(req, buf);
    }
    send_json_chunk(req, "]}");

    return httpd_resp_send_chunk(req, NULL, 0);
}

// GET /api/history/sensor?node=&endpoint=&date=&points=
static esp_err_t sensor_handler(httpd_req_t *req)
{
    char node_raw[24], ep_raw[12];
    if (!query_param(req, "node", node_raw, sizeof(node_raw)) ||
        !query_param(req, "endpoint", ep_raw, sizeof(ep_raw))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"node and endpoint are required\"}");
        return ESP_OK;
    }

    char safe_node[24], safe_ep[12];
    if (!history_sanitize_token(node_raw, safe_node, sizeof(safe_node)) ||
        !history_sanitize_token(ep_raw, safe_ep, sizeof(safe_ep))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"bad node or endpoint\"}");
        return ESP_OK;
    }

    char date[16];
    if (!requested_date(req, date, sizeof(date))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"bad or missing date\"}");
        return ESP_OK;
    }

    // The kind is not in the path; it comes from the file's own header, so any sensor
    // kind is served by the same handler.
    char path[HISTORY_PATH_MAX];
    int  n = snprintf(path, sizeof(path), "%s/sensor-%s-%s-%s", SD_CARD_MOUNT_POINT,
                      safe_node, safe_ep, date);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"path too long\"}");
        return ESP_OK;
    }

    return stream_file(req, path, requested_points(req));
}

// GET /api/history/* — routes to the two sub-resources above.
static esp_err_t history_sub_handler(httpd_req_t *req)
{
    if (!sd_card_available()) {
        return send_unavailable(req);
    }

    if (strncmp(req->uri, "/api/history/sensors", 20) == 0) {
        return sensors_handler(req);
    }
    if (strncmp(req->uri, "/api/history/sensor", 19) == 0) {
        return sensor_handler(req);
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
