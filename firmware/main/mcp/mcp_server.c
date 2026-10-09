#include "mcp_server.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "storage/history_format.h"
#include "storage/history_paths.h"
#include "storage/sd_card.h"
#include "storage/time_sync.h"

#include "mcp_cycles.h"
#include "mcp_series.h"
#include "mcp_time.h"

static const char *TAG = "mcp";

#define MCP_SERVER_NAME    "heating-monitor"
#define MCP_SERVER_VERSION "0.1.0"

// Offered when the client asks for a protocol version this server does not know.
#define MCP_PROTOCOL_VERSION "2025-06-18"

#define MCP_MAX_BODY_BYTES 8192

// The longest range one tool call may cover. A day is 484 KB at the 5 s default and the web
// server is one task, so everything else waits while a call reads its days off the card.
#define MCP_MAX_RANGE_DAYS 31

// Runs kept per summarise_cycles call. A badly short-cycling day is a couple of hundred, so this
// is a month of that. They live in PSRAM for the length of the call.
#define MCP_MAX_RUNS 4096

#define MCP_DEFAULT_ON_THRESHOLD_W 300
#define MCP_DEFAULT_MIN_GAP_S      120
#define MCP_DEFAULT_MIN_RUN_S      60
// A placeholder: callers should pass rated_output_w for the unit actually fitted.
#define MCP_DEFAULT_RATED_OUTPUT_W 8000

#define SCAN_CHUNK_RECORDS 128

// The tool definitions, embedded from mcp_tools.json so the schemas can be written as JSON
// rather than as an escaped C string. NUL-terminated by the build.
extern const char mcp_tools_json_start[] asm("_binary_mcp_tools_json_start");

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// A chunked response written a kilobyte at a time. A tool's result is JSON carried inside a JSON
// string, so with `escape` set everything written is escaped on the way through and the tool
// code can write its JSON plainly.
typedef struct {
    httpd_req_t *req;
    char         buf[1024];
    size_t       len;
    bool         escape;
} mcp_out_t;

static void out_flush(mcp_out_t *o)
{
    if (o->len) {
        httpd_resp_send_chunk(o->req, o->buf, o->len);
        o->len = 0;
    }
}

static void out_byte(mcp_out_t *o, char c)
{
    if (o->len == sizeof(o->buf)) {
        out_flush(o);
    }
    o->buf[o->len++] = c;
}

static void out_str(mcp_out_t *o, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (!o->escape) {
            out_byte(o, (char)c);
        } else if (c == '"' || c == '\\') {
            out_byte(o, '\\');
            out_byte(o, (char)c);
        } else if (c < 0x20) {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            for (const char *h = hex; *h; h++) {
                out_byte(o, *h);
            }
        } else {
            out_byte(o, (char)c);
        }
    }
}

static void out_printf(mcp_out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void out_printf(mcp_out_t *o, const char *fmt, ...)
{
    char    tmp[320];
    va_list args;

    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);

    out_str(o, tmp);
}

// A number with `decimals` places, or null.
static void out_num(mcp_out_t *o, bool has, double value, int decimals)
{
    if (has) {
        out_printf(o, "%.*f", decimals, value);
    } else {
        out_str(o, "null");
    }
}

// Opens a tool result: everything written until tool_result_end() is the text of its one content
// item.
static void tool_result_begin(mcp_out_t *o, httpd_req_t *req, const char *id)
{
    o->req    = req;
    o->len    = 0;
    o->escape = false;

    httpd_resp_set_type(req, "application/json");
    out_printf(o, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"", id);
    o->escape = true;
}

static esp_err_t tool_result_end(mcp_out_t *o, bool is_error)
{
    o->escape = false;
    out_str(o, is_error ? "\"}],\"isError\":true}}" : "\"}]}}");
    out_flush(o);
    return httpd_resp_send_chunk(o->req, NULL, 0);
}

// A tool result that is an error, with a message meant for the model: it says what to change.
static esp_err_t tool_error(httpd_req_t *req, const char *id, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static esp_err_t tool_error(httpd_req_t *req, const char *id, const char *fmt, ...)
{
    char    message[320];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // On the heap: the httpd task has 20 KB of stack and the tools are already deep in it.
    mcp_out_t *o = malloc(sizeof(mcp_out_t));
    if (!o) {
        return httpd_resp_send_500(req);
    }

    tool_result_begin(o, req, id);
    out_str(o, message);
    esp_err_t err = tool_result_end(o, true);
    free(o);
    return err;
}

// A JSON-RPC result whose body is already JSON.
static esp_err_t send_result(httpd_req_t *req, const char *id, const char *result_json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"jsonrpc\":\"2.0\",\"id\":");
    httpd_resp_sendstr_chunk(req, id);
    httpd_resp_sendstr_chunk(req, ",\"result\":");
    httpd_resp_sendstr_chunk(req, result_json);
    httpd_resp_sendstr_chunk(req, "}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t send_rpc_error(httpd_req_t *req, const char *status, const char *id, int code,
                                const char *message)
{
    char body[256];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
             id ? id : "null", code, message);

    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

// ---------------------------------------------------------------------------
// Reading the day files
// ---------------------------------------------------------------------------

// Large and short-lived, so from PSRAM where there is some.
static void *big_alloc(size_t size)
{
    void *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, size);
}

// A field of a home record, or false when it holds the sentinel.
static bool home_field(const uint8_t *rec, size_t index, int32_t *out)
{
    uint16_t raw;
    memcpy(&raw, rec + index * 2, 2);

    if (history_field_is_null(KIND_HOME, index, raw)) {
        return false;
    }

    *out = ((history_unsigned_mask(KIND_HOME) >> index) & 1u) ? (int32_t)raw : (int32_t)(int16_t)raw;
    return true;
}

// Opens the home file for the local day starting at `day` and reads its header. NULL if there is
// none this firmware can read.
static FILE *open_home_day(uint32_t day, history_header_t *hdr)
{
    char path[HISTORY_PATH_MAX];
    if (!history_path_for(KIND_HOME, 0, day, path, sizeof(path))) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fread(hdr, sizeof(*hdr), 1, f) != 1 || hdr->magic != HISTORY_MAGIC ||
        hdr->version != HISTORY_VERSION || hdr->sensor_kind != KIND_HOME ||
        hdr->record_size != sizeof(rec_home_t) || hdr->interval_s == 0) {
        fclose(f);
        return NULL;
    }
    return f;
}

// The local midnight after `day`. Thirty hours on is always inside the next local day, whether
// this one has 23, 24 or 25 hours.
static uint32_t next_day(uint32_t day)
{
    return local_midnight_for(day + 30 * 3600);
}

typedef void (*scan_cb_t)(void *ctx, uint32_t ts, uint16_t interval_s, const uint8_t *rec);

typedef struct {
    uint32_t files;
    uint32_t samples;
} scan_stats_t;

// Calls `cb` for every recorded slot with start <= ts < end, in time order, across as many day
// files as the range touches. False only if the read buffer could not be allocated.
static bool scan_range(uint32_t start, uint32_t end, scan_cb_t cb, void *ctx, scan_stats_t *stats)
{
    uint8_t *buf = malloc(SCAN_CHUNK_RECORDS * sizeof(rec_home_t));
    if (!buf) {
        return false;
    }

    memset(stats, 0, sizeof(*stats));

    for (uint32_t day = local_midnight_for(start); day < end; day = next_day(day)) {
        history_header_t hdr;
        FILE            *f = open_home_day(day, &hdr);
        if (!f) {
            continue;
        }
        stats->files++;

        // Slot arithmetic rather than a search: slot i is base_ts + i * interval_s.
        uint32_t slot = 0;
        if (start > hdr.base_ts) {
            slot = (start - hdr.base_ts + hdr.interval_s - 1) / hdr.interval_s;
        }
        fseek(f, (long)(sizeof(hdr) + (size_t)slot * sizeof(rec_home_t)), SEEK_SET);

        bool   done = false;
        size_t got;
        while (!done && (got = fread(buf, sizeof(rec_home_t), SCAN_CHUNK_RECORDS, f)) > 0) {
            for (size_t i = 0; i < got; i++, slot++) {
                uint32_t ts = hdr.base_ts + slot * hdr.interval_s;
                if (ts >= end) {
                    done = true;
                    break;
                }
                cb(ctx, ts, hdr.interval_s, buf + i * sizeof(rec_home_t));
                stats->samples++;
            }
        }
        fclose(f);
    }

    free(buf);
    return true;
}

// The sampling interval of the first day file in a range, or 0 if no day in it has one.
static uint16_t native_interval(uint32_t start, uint32_t end)
{
    for (uint32_t day = local_midnight_for(start); day < end; day = next_day(day)) {
        history_header_t hdr;
        FILE            *f = open_home_day(day, &hdr);
        if (f) {
            fclose(f);
            return hdr.interval_s;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

// The `timezone` argument: true for UTC, false for the device's own (UK) time. An unknown zone
// is an error the caller can act on.
static bool zone_arg(const cJSON *args, bool *utc, const char **error)
{
    const cJSON *zone = cJSON_GetObjectItemCaseSensitive(args, "timezone");

    *utc = false;
    if (zone == NULL || cJSON_IsNull(zone)) {
        return true;
    }
    if (cJSON_IsString(zone)) {
        if (strcmp(zone->valuestring, "Europe/London") == 0) {
            return true;
        }
        if (strcmp(zone->valuestring, "UTC") == 0 || strcmp(zone->valuestring, "Etc/UTC") == 0) {
            *utc = true;
            return true;
        }
    }

    *error = "This device only knows the timezones Europe/London and UTC. For any other zone, "
             "pass start and end with an explicit offset such as 2026-10-08T06:00:00+02:00.";
    return false;
}

// `start` and `end` out of `from`, checked. On failure `error` says why.
static bool range_arg(const cJSON *from, bool utc, uint32_t *start, uint32_t *end, const char **error)
{
    const cJSON *start_json = cJSON_GetObjectItemCaseSensitive(from, "start");
    const cJSON *end_json   = cJSON_GetObjectItemCaseSensitive(from, "end");

    if (!cJSON_IsString(start_json) || !cJSON_IsString(end_json)) {
        *error = "`start` and `end` are required, as ISO 8601 strings.";
        return false;
    }
    if (!mcp_time_parse(start_json->valuestring, utc, start) ||
        !mcp_time_parse(end_json->valuestring, utc, end)) {
        *error = "`start` and `end` must be ISO 8601 timestamps, e.g. 2026-10-08T06:00 or "
                 "2026-10-08T06:00:00+01:00.";
        return false;
    }
    if (*end <= *start) {
        *error = "`end` must be after `start`.";
        return false;
    }
    if (*end - *start > MCP_MAX_RANGE_DAYS * 86400u) {
        *error = "That range is longer than 31 days, the most one call may cover. Split it into "
                 "shorter ranges.";
        return false;
    }
    return true;
}

// An optional number. False if it is present but not a number in [min, max].
static bool number_arg(const cJSON *args, const char *name, double min, double max, double fallback,
                       double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(args, name);

    if (item == NULL || cJSON_IsNull(item)) {
        *out = fallback;
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < min || item->valuedouble > max) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

// ---------------------------------------------------------------------------
// list_series
// ---------------------------------------------------------------------------

static esp_err_t tool_list_series(httpd_req_t *req, const char *id)
{
    // The first and last days on the card. ISO dates sort as strings.
    char earliest_date[16] = "";
    char latest_date[16]   = "";

    DIR *dir = opendir(SD_CARD_MOUNT_POINT);
    if (!dir) {
        return tool_error(req, id, "The SD card could not be read, so there is no history.");
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char date[16];
        if (sscanf(ent->d_name, "home-%15s", date) != 1 || strlen(date) != 10) {
            continue;
        }
        if (earliest_date[0] == '\0' || strcmp(date, earliest_date) < 0) {
            strlcpy(earliest_date, date, sizeof(earliest_date));
        }
        if (strcmp(date, latest_date) > 0) {
            strlcpy(latest_date, date, sizeof(latest_date));
        }
    }
    closedir(dir);

    mcp_out_t *o = malloc(sizeof(mcp_out_t));
    if (!o) {
        return httpd_resp_send_500(req);
    }
    tool_result_begin(o, req, id);

    // A day file starts at local midnight and pads the slots before its first reading, so the
    // first day's midnight is where the series begin. The last one ends where its file does.
    uint32_t earliest = 0;
    uint32_t latest   = 0;
    uint16_t interval = 0;
    char     path[HISTORY_PATH_MAX];

    if (latest_date[0] && mcp_time_parse(earliest_date, false, &earliest) &&
        history_path_for_date(KIND_HOME, 0, latest_date, path, sizeof(path))) {
        uint32_t         latest_day;
        history_header_t hdr;
        struct stat      st;

        if (mcp_time_parse(latest_date, false, &latest_day) && stat(path, &st) == 0) {
            FILE *f = open_home_day(latest_day, &hdr);
            if (f) {
                fclose(f);
                interval = hdr.interval_s;
                latest   = hdr.base_ts +
                           (uint32_t)((st.st_size - sizeof(hdr)) / sizeof(rec_home_t)) * hdr.interval_s;
            }
        }
    }

    char when[32];

    out_str(o, "{\"native_interval_seconds\":");
    out_num(o, interval != 0, interval, 0);

    out_str(o, ",\"earliest\":");
    if (latest) {
        mcp_time_format(earliest, false, when, sizeof(when));
        out_printf(o, "\"%s\"", when);
    } else {
        out_str(o, "null");
    }

    out_str(o, ",\"latest\":");
    if (latest) {
        mcp_time_format(latest, false, when, sizeof(when));
        out_printf(o, "\"%s\"", when);
    } else {
        out_str(o, "null");
    }

    out_str(o, ",\"series\":[");
    for (size_t i = 0; i < MCP_SERIES_COUNT; i++) {
        const mcp_series_t *s = &MCP_SERIES[i];

        out_printf(o, "%s{\"id\":\"%s\",\"name\":\"%s\",\"unit\":\"%s\"", i ? "," : "", s->id,
                   s->name, s->unit);
        if (s->note) {
            out_str(o, ",\"note\":\"");
            out_str(o, s->note);
            out_str(o, "\"");
        }
        out_str(o, "}");
    }
    out_str(o, "]}");

    esp_err_t err = tool_result_end(o, false);
    free(o);
    return err;
}

// ---------------------------------------------------------------------------
// get_series
// ---------------------------------------------------------------------------

typedef enum { AGG_MEAN, AGG_MAX, AGG_MIN, AGG_LAST } aggregation_t;

typedef struct {
    uint32_t            start;
    uint32_t            interval;
    uint32_t            buckets;
    size_t              series_count;
    const mcp_series_t *series[16];
    aggregation_t       aggregation;
    // series_count x buckets each. `count` is what says a bucket has a reading at all.
    int64_t  *acc;
    uint32_t *count;
} resample_t;

static void resample_cb(void *ctx, uint32_t ts, uint16_t interval_s, const uint8_t *rec)
{
    resample_t *r = ctx;
    uint32_t    b = (ts - r->start) / r->interval;

    (void)interval_s;
    if (b >= r->buckets) {
        return;
    }

    for (size_t s = 0; s < r->series_count; s++) {
        int32_t value;
        if (!home_field(rec, r->series[s]->field, &value)) {
            continue;
        }

        size_t at = s * r->buckets + b;

        if (r->count[at] == 0 || r->aggregation == AGG_LAST) {
            r->acc[at] = value; // samples arrive in time order, so the latest is the last
        } else if (r->aggregation == AGG_MEAN) {
            r->acc[at] += value;
        } else if (r->aggregation == AGG_MAX) {
            r->acc[at] = value > r->acc[at] ? value : r->acc[at];
        } else {
            r->acc[at] = value < r->acc[at] ? value : r->acc[at];
        }
        r->count[at]++;
    }
}

static esp_err_t tool_get_series(httpd_req_t *req, const char *id, const cJSON *args)
{
    static const char *const AGGREGATIONS[] = {"mean", "max", "min", "last"};

    const char *error = NULL;
    bool        utc;
    uint32_t    start, end;

    if (!zone_arg(args, &utc, &error) || !range_arg(args, utc, &start, &end, &error)) {
        return tool_error(req, id, "%s", error);
    }

    resample_t r = {.start = start, .aggregation = AGG_MEAN};

    const cJSON *series = cJSON_GetObjectItemCaseSensitive(args, "series");
    const cJSON *item;

    if (!cJSON_IsArray(series) || cJSON_GetArraySize(series) == 0) {
        return tool_error(req, id, "`series` is required: an array of ids from list_series.");
    }
    cJSON_ArrayForEach(item, series)
    {
        const mcp_series_t *found = cJSON_IsString(item) ? mcp_series_find(item->valuestring) : NULL;
        if (!found) {
            return tool_error(req, id, "Unknown series \"%s\". Call list_series for the ids.",
                              cJSON_IsString(item) ? item->valuestring : "?");
        }
        if (r.series_count == sizeof(r.series) / sizeof(r.series[0])) {
            return tool_error(req, id, "Too many series in one call; ask for 16 or fewer.");
        }
        r.series[r.series_count++] = found;
    }

    const cJSON *aggregation = cJSON_GetObjectItemCaseSensitive(args, "aggregation");
    if (aggregation != NULL && !cJSON_IsNull(aggregation)) {
        bool known = false;
        for (int a = 0; cJSON_IsString(aggregation) && a < 4; a++) {
            if (strcmp(aggregation->valuestring, AGGREGATIONS[a]) == 0) {
                r.aggregation = (aggregation_t)a;
                known         = true;
            }
        }
        if (!known) {
            return tool_error(req, id, "`aggregation` must be one of mean, max, min, last.");
        }
    }

    double requested;
    if (!number_arg(args, "interval_seconds", 1, 86400.0 * MCP_MAX_RANGE_DAYS, 0, &requested)) {
        return tool_error(req, id, "`interval_seconds` must be a whole number of seconds, 1 or more.");
    }

    uint16_t native = native_interval(start, end);

    if (mcp_plan_interval(end - start, (uint32_t)requested, native, &r.interval) ==
        MCP_PLAN_TOO_MANY_POINTS) {
        return tool_error(req, id,
                          "That is more than %d points per series, which is the limit. Use "
                          "interval_seconds of at least %lu, or omit it to have one chosen, or "
                          "shorten the range.",
                          MCP_HARD_MAX_POINTS, (unsigned long)r.interval);
    }

    r.buckets = (end - start + r.interval - 1) / r.interval;
    r.acc     = big_alloc(r.series_count * r.buckets * sizeof(int64_t));
    r.count   = big_alloc(r.series_count * r.buckets * sizeof(uint32_t));

    mcp_out_t   *o = malloc(sizeof(mcp_out_t));
    scan_stats_t stats;

    if (!r.acc || !r.count || !o || !scan_range(start, end, resample_cb, &r, &stats)) {
        free(r.acc);
        free(r.count);
        free(o);
        return tool_error(req, id, "The device ran out of memory. Ask for fewer series or a coarser interval.");
    }

    tool_result_begin(o, req, id);
    out_printf(o, "{\"interval_seconds\":%lu,\"aggregation\":\"%s\",\"timestamps\":[",
               (unsigned long)r.interval, AGGREGATIONS[r.aggregation]);

    for (uint32_t b = 0; b < r.buckets; b++) {
        char when[32];
        mcp_time_format(start + b * r.interval, utc, when, sizeof(when));
        out_printf(o, "%s\"%s\"", b ? "," : "", when);
    }

    out_str(o, "],\"series\":{");
    for (size_t s = 0; s < r.series_count; s++) {
        const mcp_series_t *def = r.series[s];

        out_printf(o, "%s\"%s\":[", s ? "," : "", def->id);
        for (uint32_t b = 0; b < r.buckets; b++) {
            size_t at = s * r.buckets + b;
            double value = (double)r.acc[at];

            if (r.count[at] && r.aggregation == AGG_MEAN) {
                value /= r.count[at];
            }
            if (b) {
                out_str(o, ",");
            }
            out_num(o, r.count[at] != 0, value / def->divisor, def->decimals);
        }
        out_str(o, "]");
    }
    out_str(o, "}");

    if (stats.files == 0) {
        out_str(o, ",\"notes\":[\"Nothing was recorded on any day in this range.\"]");
    }
    out_str(o, "}");

    esp_err_t err = tool_result_end(o, false);

    free(r.acc);
    free(r.count);
    free(o);
    return err;
}

// ---------------------------------------------------------------------------
// summarise_cycles and compare_periods
// ---------------------------------------------------------------------------

static void cycle_cb(void *ctx, uint32_t ts, uint16_t interval_s, const uint8_t *rec)
{
    int32_t        heating, dhw;
    cycle_sample_t s = {.ts = ts, .step_s = interval_s};

    s.has_heat    = home_field(rec, 0, &s.heat_w);
    s.has_elec    = home_field(rec, 1, &s.elec_w);
    s.has_flow    = home_field(rec, 2, &s.flow_c100);
    s.has_return  = home_field(rec, 3, &s.return_c100);
    s.has_outdoor = home_field(rec, 5, &s.outdoor_c100);
    s.dhw         = home_field(rec, 8, &dhw) ? (int8_t)dhw : -1;
    s.heating     = home_field(rec, 11, &heating) ? (int8_t)heating : -1;

    cycle_add_sample(ctx, &s);
}

// The thresholds a call asked for, or the defaults.
static bool cycle_options_arg(const cJSON *args, cycle_options_t *opt, const char **error)
{
    double on, gap, run, rated;

    if (!number_arg(args, "on_threshold_w", 1, 32000, MCP_DEFAULT_ON_THRESHOLD_W, &on) ||
        !number_arg(args, "min_gap_seconds", 0, 86400, MCP_DEFAULT_MIN_GAP_S, &gap) ||
        !number_arg(args, "min_run_seconds", 0, 86400, MCP_DEFAULT_MIN_RUN_S, &run) ||
        !number_arg(args, "rated_output_w", 1, 1000000, MCP_DEFAULT_RATED_OUTPUT_W, &rated)) {
        *error = "on_threshold_w, min_gap_seconds, min_run_seconds and rated_output_w must be "
                 "numbers: watts above zero, and seconds of zero or more.";
        return false;
    }

    opt->on_threshold_w = (uint32_t)on;
    opt->min_gap_s      = (uint32_t)gap;
    opt->min_run_s      = (uint32_t)run;
    opt->rated_output_w = (uint32_t)rated;
    return true;
}

// One period's cycles, worked out. `state` and its runs are the caller's to free with
// cycles_free().
typedef struct {
    cycle_state_t  *state;
    cycle_run_t    *runs;
    cycle_summary_t summary;
    scan_stats_t    stats;
} cycles_t;

static void cycles_free(cycles_t *c)
{
    free(c->state);
    free(c->runs);
    c->state = NULL;
    c->runs  = NULL;
}

static bool cycles_run(cycles_t *c, uint32_t start, uint32_t end, const cycle_options_t *opt)
{
    c->state = big_alloc(sizeof(cycle_state_t));
    c->runs  = big_alloc(MCP_MAX_RUNS * sizeof(cycle_run_t));

    if (c->state && c->runs) {
        cycle_init(c->state, opt, start, end, c->runs, MCP_MAX_RUNS);

        if (scan_range(start, end, cycle_cb, c->state, &c->stats)) {
            cycle_finish(c->state);
            if (cycle_summarise(c->state, &c->summary)) {
                return true;
            }
        }
    }

    cycles_free(c);
    return false;
}

static void write_period(mcp_out_t *o, uint32_t start, uint32_t end, bool utc)
{
    char when[32];

    mcp_time_format(start, utc, when, sizeof(when));
    out_printf(o, "\"start\":\"%s\",", when);
    mcp_time_format(end, utc, when, sizeof(when));
    out_printf(o, "\"end\":\"%s\"", when);
}

static void write_summary(mcp_out_t *o, const cycle_summary_t *s)
{
    out_printf(o, "\"summary\":{\"period_hours\":%.2f,\"data_coverage_pct\":%.1f,\"mode_recorded\":%s,",
               s->period_hours, s->data_coverage_pct, s->mode_recorded ? "true" : "false");
    out_printf(o, "\"runs\":%lu,\"heating_runs\":%lu,\"dhw_runs\":%lu,", (unsigned long)s->runs,
               (unsigned long)s->heating_runs, (unsigned long)s->dhw_runs);

    out_str(o, "\"heating_starts_per_hour\":");
    out_num(o, s->has_heating_starts_per_hour, s->heating_starts_per_hour, 2);
    out_str(o, ",\"median_run_min\":");
    out_num(o, s->has_median_run, s->median_run_min, 1);
    out_str(o, ",\"median_heating_run_min\":");
    out_num(o, s->has_median_heating_run, s->median_heating_run_min, 1);
    out_str(o, ",\"median_off_min\":");
    out_num(o, s->has_median_off, s->median_off_min, 1);
    out_str(o, ",\"cop_all_runs\":");
    out_num(o, s->has_cop_all, s->cop_all, 2);
    out_str(o, ",\"cop_heating\":");
    out_num(o, s->has_cop_heating, s->cop_heating, 2);
    out_str(o, ",\"cop_dhw\":");
    out_num(o, s->has_cop_dhw, s->cop_dhw, 2);

    out_printf(o, ",\"heat_kwh\":%.3f,\"elec_kwh\":%.3f,\"heating_heat_kwh\":%.3f,\"heating_elec_kwh\":%.3f,",
               s->heat_kwh, s->elec_kwh, s->heating_heat_kwh, s->heating_elec_kwh);
    out_printf(o, "\"dhw_heat_kwh\":%.3f,\"dhw_elec_kwh\":%.3f,\"elec_kwh_outside_runs\":%.3f,",
               s->dhw_heat_kwh, s->dhw_elec_kwh, s->elec_kwh_outside_runs);

    out_str(o, "\"mean_outdoor_c\":");
    out_num(o, s->has_mean_outdoor, s->mean_outdoor_c, 1);
    out_printf(o, ",\"excluded_heat_samples\":%lu}", (unsigned long)s->excluded_heat_samples);
}

// What a reader needs to know before trusting the figures. Writes nothing when there is nothing.
static void write_notes(mcp_out_t *o, const cycles_t *c)
{
    const char *notes[4];
    size_t      n = 0;

    if (c->stats.files == 0) {
        notes[n++] = "Nothing was recorded on any day in this period.";
    } else {
        if (!c->summary.mode_recorded) {
            notes[n++] = "No operating mode was recorded in this period, so runs have mode unknown "
                         "and there is no heating / hot water split.";
        }
        if (c->summary.data_coverage_pct < 99.0) {
            notes[n++] = "Part of the period has no electrical readings; the figures describe "
                         "only the part that does. See data_coverage_pct.";
        }
        if (c->summary.truncated) {
            notes[n++] = "There were more runs than this device can hold for one call, and the "
                         "later ones are missing from every figure. Use a shorter period.";
        }
    }

    if (n == 0) {
        return;
    }

    out_str(o, ",\"notes\":[");
    for (size_t i = 0; i < n; i++) {
        out_printf(o, "%s\"%s\"", i ? "," : "", notes[i]);
    }
    out_str(o, "]");
}

static void write_run(mcp_out_t *o, const cycle_run_t *run, bool utc)
{
    out_str(o, "{");
    write_period(o, run->start_ts, run->end_ts, utc);

    out_printf(o, ",\"duration_min\":%.1f,\"mode\":\"%s\",\"compressor_start\":%s,",
               (run->end_ts - run->start_ts) / 60.0, cycle_mode_name(run->mode),
               run->compressor_start ? "true" : "false");

    out_str(o, "\"avg_heat_w\":");
    out_num(o, run->heat_n != 0, run->heat_n ? (double)run->heat_sum_w / run->heat_n : 0, 0);
    out_str(o, ",\"max_heat_w\":");
    out_num(o, run->heat_n != 0, run->max_heat_w, 0);
    out_str(o, ",\"avg_elec_w\":");
    out_num(o, run->elec_n != 0, run->elec_n ? (double)run->elec_sum_w / run->elec_n : 0, 0);

    out_printf(o, ",\"heat_kwh\":%.3f,\"elec_kwh\":%.3f,\"cop\":", run->heat_ws / 3.6e6,
               run->elec_ws / 3.6e6);
    out_num(o, run->cop_elec_ws > 0,
            run->cop_elec_ws > 0 ? (double)run->cop_heat_ws / (double)run->cop_elec_ws : 0, 2);

    out_str(o, ",\"avg_flow_c\":");
    out_num(o, run->flow_n != 0, run->flow_n ? run->flow_sum_c100 / 100.0 / run->flow_n : 0, 1);
    out_str(o, ",\"avg_return_c\":");
    out_num(o, run->return_n != 0, run->return_n ? run->return_sum_c100 / 100.0 / run->return_n : 0, 1);
    out_str(o, ",\"avg_outdoor_c\":");
    out_num(o, run->outdoor_n != 0, run->outdoor_n ? run->outdoor_sum_c100 / 100.0 / run->outdoor_n : 0, 1);
    out_str(o, ",\"flow_at_stop_c\":");
    out_num(o, run->has_flow_at_stop, run->flow_at_stop_c100 / 100.0, 1);
    out_str(o, ",\"off_time_after_min\":");
    out_num(o, run->off_after_s >= 0, run->off_after_s / 60.0, 1);

    out_printf(o, ",\"excluded_heat_samples\":%lu,\"clipped_by_period\":%s}",
               (unsigned long)run->excluded, run->clipped ? "true" : "false");
}

static esp_err_t tool_summarise_cycles(httpd_req_t *req, const char *id, const cJSON *args)
{
    const char     *error = NULL;
    bool            utc;
    uint32_t        start, end;
    cycle_options_t opt;

    if (!zone_arg(args, &utc, &error) || !range_arg(args, utc, &start, &end, &error) ||
        !cycle_options_arg(args, &opt, &error)) {
        return tool_error(req, id, "%s", error);
    }

    const cJSON *include = cJSON_GetObjectItemCaseSensitive(args, "include_runs");
    bool         include_runs = !cJSON_IsFalse(include);

    cycles_t   c = {0};
    mcp_out_t *o = malloc(sizeof(mcp_out_t));

    if (!o || !cycles_run(&c, start, end, &opt)) {
        free(o);
        return tool_error(req, id, "The device ran out of memory working that out. Use a shorter period.");
    }

    tool_result_begin(o, req, id);

    out_str(o, "{");
    write_period(o, start, end, utc);
    out_printf(o, ",\"thresholds\":{\"on_threshold_w\":%lu,\"min_gap_seconds\":%lu,"
                  "\"min_run_seconds\":%lu,\"rated_output_w\":%lu},",
               (unsigned long)opt.on_threshold_w, (unsigned long)opt.min_gap_s,
               (unsigned long)opt.min_run_s, (unsigned long)opt.rated_output_w);
    write_summary(o, &c.summary);

    if (include_runs) {
        out_str(o, ",\"runs\":[");
        for (size_t i = 0; i < c.state->count; i++) {
            if (i) {
                out_str(o, ",");
            }
            write_run(o, &c.runs[i], utc);
        }
        out_str(o, "]");
    }

    write_notes(o, &c);
    out_str(o, "}");

    esp_err_t err = tool_result_end(o, false);

    cycles_free(&c);
    free(o);
    return err;
}

static esp_err_t tool_compare_periods(httpd_req_t *req, const char *id, const cJSON *args)
{
    static const char *const KEYS[] = {"period_a", "period_b"};

    const char     *error = NULL;
    bool            utc;
    uint32_t        start[2], end[2];
    cycle_options_t opt;

    if (!zone_arg(args, &utc, &error) || !cycle_options_arg(args, &opt, &error)) {
        return tool_error(req, id, "%s", error);
    }

    for (int p = 0; p < 2; p++) {
        const cJSON *period = cJSON_GetObjectItemCaseSensitive(args, KEYS[p]);
        if (!cJSON_IsObject(period)) {
            return tool_error(req, id, "`period_a` and `period_b` are required, each an object "
                                       "with `start` and `end`.");
        }
        if (!range_arg(period, utc, &start[p], &end[p], &error)) {
            return tool_error(req, id, "%s: %s", KEYS[p], error);
        }
    }

    // Both worked out before anything is sent, so a failure on the second can still be reported.
    // Only the summaries are kept: the runs are freed as each period is done.
    cycles_t periods[2] = {0};

    for (int p = 0; p < 2; p++) {
        if (!cycles_run(&periods[p], start[p], end[p], &opt)) {
            return tool_error(req, id, "The device ran out of memory working that out. Use shorter periods.");
        }
        cycles_free(&periods[p]);
    }

    mcp_out_t *o = malloc(sizeof(mcp_out_t));
    if (!o) {
        return httpd_resp_send_500(req);
    }

    tool_result_begin(o, req, id);
    out_str(o, "{");

    for (int p = 0; p < 2; p++) {
        out_printf(o, "%s\"%s\":{", p ? "," : "", KEYS[p]);
        write_period(o, start[p], end[p], utc);
        out_str(o, ",");
        write_summary(o, &periods[p].summary);
        write_notes(o, &periods[p]);
        out_str(o, "}");
    }
    out_str(o, "}");

    esp_err_t err = tool_result_end(o, false);
    free(o);
    return err;
}

// ---------------------------------------------------------------------------
// JSON-RPC
// ---------------------------------------------------------------------------

static esp_err_t handle_initialize(httpd_req_t *req, const char *id, const cJSON *params)
{
    static const char *const KNOWN[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

    // The client's version if this server speaks it, otherwise the one this server prefers and
    // the client decides whether it can live with that.
    const char  *version   = MCP_PROTOCOL_VERSION;
    const cJSON *requested = cJSON_GetObjectItemCaseSensitive(params, "protocolVersion");

    for (size_t i = 0; cJSON_IsString(requested) && i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++) {
        if (strcmp(requested->valuestring, KNOWN[i]) == 0) {
            version = KNOWN[i];
        }
    }

    char result[768];
    snprintf(result, sizeof(result),
             "{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":{}},"
             "\"serverInfo\":{\"name\":\"" MCP_SERVER_NAME "\",\"version\":\"" MCP_SERVER_VERSION "\"},"
             "\"instructions\":\"Read-only access to a home heat pump monitor's recorded history: "
             "heat and electrical power, flow, return, outdoor and indoor temperatures, flow rate "
             "and operating mode, sampled every few seconds. Use summarise_cycles for cycling and "
             "COP questions and compare_periods for before/after comparisons; get_series is for "
             "plotting or inspecting a stretch directly.\"}",
             version);

    return send_result(req, id, result);
}

static esp_err_t handle_tools_call(httpd_req_t *req, const char *id, const cJSON *params)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "arguments");

    if (!cJSON_IsString(name)) {
        return send_rpc_error(req, "200 OK", id, -32602, "Missing tool name");
    }

    bool is_list    = strcmp(name->valuestring, "list_series") == 0;
    bool is_get     = strcmp(name->valuestring, "get_series") == 0;
    bool is_cycles  = strcmp(name->valuestring, "summarise_cycles") == 0;
    bool is_compare = strcmp(name->valuestring, "compare_periods") == 0;

    if (!is_list && !is_get && !is_cycles && !is_compare) {
        return send_rpc_error(req, "200 OK", id, -32602, "Unknown tool");
    }

    if (!sd_card_available()) {
        return tool_error(req, id, "The monitor has no SD card mounted, so there is no history to read.");
    }

    ESP_LOGI(TAG, "tools/call %s", name->valuestring);

    if (is_list) {
        return tool_list_series(req, id);
    }
    if (is_get) {
        return tool_get_series(req, id, args);
    }
    if (is_cycles) {
        return tool_summarise_cycles(req, id, args);
    }
    return tool_compare_periods(req, id, args);
}

static esp_err_t mcp_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > MCP_MAX_BODY_BYTES) {
        return send_rpc_error(req, "400 Bad Request", NULL, -32700, "Body missing or too large");
    }

    char *body = malloc(req->content_len + 1);
    if (!body) {
        return httpd_resp_send_500(req);
    }

    size_t received = 0;
    while (received < req->content_len) {
        int got = httpd_req_recv(req, body + received, req->content_len - received);
        if (got <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += (size_t)got;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);

    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (!cJSON_IsObject(root) || !cJSON_IsString(method)) {
        cJSON_Delete(root);
        return send_rpc_error(req, "400 Bad Request", NULL, -32700, "Not a JSON-RPC request");
    }

    // No id makes it a notification, which is acknowledged and gets no reply.
    const cJSON *id_json = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (id_json == NULL || cJSON_IsNull(id_json)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "202 Accepted");
        return httpd_resp_send(req, NULL, 0);
    }

    // Echoed back as it came, since an id may be a number or a string.
    char *id = cJSON_PrintUnformatted(id_json);
    if (!id) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }

    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    esp_err_t    err;

    if (strcmp(method->valuestring, "initialize") == 0) {
        err = handle_initialize(req, id, params);
    } else if (strcmp(method->valuestring, "ping") == 0) {
        err = send_result(req, id, "{}");
    } else if (strcmp(method->valuestring, "tools/list") == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr_chunk(req, "{\"jsonrpc\":\"2.0\",\"id\":");
        httpd_resp_sendstr_chunk(req, id);
        httpd_resp_sendstr_chunk(req, ",\"result\":{\"tools\":");
        httpd_resp_sendstr_chunk(req, mcp_tools_json_start);
        httpd_resp_sendstr_chunk(req, "}}");
        err = httpd_resp_send_chunk(req, NULL, 0);
    } else if (strcmp(method->valuestring, "tools/call") == 0) {
        err = handle_tools_call(req, id, params);
    } else {
        err = send_rpc_error(req, "200 OK", id, -32601, "Method not found");
    }

    free(id);
    cJSON_Delete(root);
    return err;
}

// There are no sessions, so no server-to-client stream for a GET to open. 405 is what the
// transport specifies for that, and without this handler the request would fall through to the
// web app's wildcard and be answered with index.html.
static esp_err_t mcp_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "405 Method Not Allowed");
    httpd_resp_set_hdr(req, "Allow", "POST");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"error\":\"This MCP endpoint takes POST only\"}");
}

esp_err_t mcp_server_register(httpd_handle_t server)
{
    const httpd_uri_t post_uri = {
        .uri      = "/mcp",
        .method   = HTTP_POST,
        .handler  = mcp_post_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t get_uri = {
        .uri      = "/mcp",
        .method   = HTTP_GET,
        .handler  = mcp_get_handler,
        .user_ctx = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(server, &post_uri);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_register_uri_handler(server, &get_uri);
}
