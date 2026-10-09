// Host test for the MCP server's HTTP and JSON-RPC layer: the real mcp_server.c, driven through
// a stand-in HTTP server against a day file written to a temporary "SD card". Every response is
// parsed back as JSON, and every tool result's text parsed again, so a stray comma or a missed
// escape fails here rather than on the board.
//
// Build and run with ./run.sh.

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_server.h"

#include "mcp/mcp_server.h"
#include "storage/history_format.h"
#include "storage/history_paths.h"
#include "storage/time_sync.h"

#define FIXTURE_DIR "fixture_sdcard"
#define STEP_S      5
#define NIGHT       1768435200u // 2026-01-15T00:00:00Z, also local midnight
#define AT(h, m)    (NIGHT + (h) * 3600u + (m) * 60u)

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                \
    do {                                                           \
        s_checks++;                                                \
        if (!(cond)) {                                             \
            s_failures++;                                          \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

// --- What mcp_server.c links against on the board -------------------------

static esp_err_t (*s_post)(httpd_req_t *);
static esp_err_t (*s_get)(httpd_req_t *);
static bool s_card = true;

esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri)
{
    (void)server;
    if (uri->method == HTTP_POST) {
        s_post = uri->handler;
    } else {
        s_get = uri->handler;
    }
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *req, char *buf, size_t len)
{
    // A few bytes at a time, as a socket would, so the handler's read loop is exercised.
    size_t n = req->content_len - req->read;
    if (n > len) n = len;
    if (n > 7) n = 7;
    memcpy(buf, req->body + req->read, n);
    req->read += n;
    return (int)n;
}

static void append(httpd_req_t *req, const char *buf, size_t len)
{
    req->out = realloc(req->out, req->out_len + len + 1);
    memcpy(req->out + req->out_len, buf, len);
    req->out_len += len;
    req->out[req->out_len] = '\0';
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) { (void)req; (void)type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *f, const char *v) { (void)req; (void)f; (void)v; return ESP_OK; }

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{
    snprintf(req->status, sizeof(req->status), "%s", status);
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, long len)
{
    if (buf) {
        append(req, buf, len < 0 ? strlen(buf) : (size_t)len);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *req, const char *str) { return httpd_resp_send_chunk(req, str, -1); }
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str) { return httpd_resp_send_chunk(req, str, -1); }
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, long len) { return httpd_resp_send_chunk(req, buf, len); }

esp_err_t httpd_resp_send_500(httpd_req_t *req)
{
    return httpd_resp_set_status(req, "500 Internal Server Error");
}

bool sd_card_available(void) { return s_card; }
bool clock_is_valid(void) { return true; }

uint32_t local_midnight_for(uint32_t ts)
{
    time_t    t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return (uint32_t)mktime(&tm);
}

void local_date_string(uint32_t ts, char *buf, size_t len)
{
    time_t    t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d", &tm);
}

bool history_path_for_date(uint8_t kind, uint8_t inst, const char *date, char *out, size_t out_len)
{
    (void)kind; (void)inst;
    return snprintf(out, out_len, FIXTURE_DIR "/home-%s", date) < (int)out_len;
}

bool history_path_for(uint8_t kind, uint8_t inst, uint32_t base_ts, char *out, size_t out_len)
{
    char date[16];
    local_date_string(base_ts, date, sizeof(date));
    return history_path_for_date(kind, inst, date, out, out_len);
}

// mcp_server.c is compiled with opendir renamed to this, so that its opendir("/sdcard") lands on
// the fixture directory.
DIR *test_opendir(const char *path)
{
    (void)path;
    return opendir(FIXTURE_DIR);
}


// --- The fixture: the synthetic night from test_mcp.c, as a day file -----------

static bool within(uint32_t ts, uint32_t from, uint32_t to) { return ts >= from && ts < to; }

static void write_night(void)
{
    mkdir(FIXTURE_DIR, 0755);

    FILE *f = fopen(FIXTURE_DIR "/home-2026-01-15", "wb");
    history_header_t hdr = {.magic = HISTORY_MAGIC, .version = HISTORY_VERSION, .sensor_kind = KIND_HOME,
                            .interval_s = STEP_S, .base_ts = NIGHT, .record_size = sizeof(rec_home_t)};
    fwrite(&hdr, sizeof(hdr), 1, f);

    for (uint32_t ts = NIGHT; ts < AT(6, 0); ts += STEP_S) {
        bool heating = within(ts, AT(0, 0), AT(1, 10)) || within(ts, AT(2, 35), AT(3, 10)) ||
                       within(ts, AT(3, 35), AT(4, 10)) || within(ts, AT(4, 35), AT(5, 10)) ||
                       within(ts, AT(5, 35), AT(6, 0));
        bool dhw  = within(ts, AT(1, 35), AT(2, 35));
        bool blip = within(ts, AT(1, 20), AT(1, 22)) || within(ts, AT(3, 20), AT(3, 22)) ||
                    within(ts, AT(4, 20), AT(4, 22)) || within(ts, AT(5, 20), AT(5, 22));

        rec_home_t r;
        history_fill_sentinel(KIND_HOME, &r);
        r.elec_power_w      = dhw ? 2500 : heating ? 700 : 20;
        r.heat_power_w      = ts == AT(2, 35) ? 30000 : dhw ? 6000 : heating ? 3500 : blip ? 2000 : 0;
        r.flow_temp_c100    = dhw ? 5200 : heating ? 3500 : 2800;
        r.return_temp_c100  = dhw ? 4600 : heating ? 3000 : 2700;
        r.outdoor_temp_c100 = 400;
        r.heating_running   = heating ? 1 : 0;
        r.dhw_running       = dhw ? 1 : 0;
        fwrite(&r, sizeof(r), 1, f);
    }
    fclose(f);
}

// --- Driving it ---------------------------------------------------------------

typedef struct {
    char   status[64];
    cJSON *json;   // the whole JSON-RPC response, NULL if it was not JSON
    cJSON *result; // a tool result's text parsed as JSON, NULL if it was not
    const char *text;
    bool   is_error;
} reply_t;

static reply_t post(const char *body)
{
    httpd_req_t req = {.content_len = strlen(body), .body = body};
    reply_t     reply = {0};

    CHECK(s_post(&req) == ESP_OK);

    snprintf(reply.status, sizeof(reply.status), "%s", req.status);
    reply.json = req.out ? cJSON_Parse(req.out) : NULL;

    const cJSON *result  = cJSON_GetObjectItem(reply.json, "result");
    const cJSON *content = cJSON_GetArrayItem(cJSON_GetObjectItem(result, "content"), 0);
    const cJSON *text    = cJSON_GetObjectItem(content, "text");

    if (cJSON_IsString(text)) {
        reply.text     = text->valuestring;
        reply.result   = cJSON_Parse(text->valuestring);
        reply.is_error = cJSON_IsTrue(cJSON_GetObjectItem(result, "isError"));
    }

    free(req.out);
    return reply;
}

static reply_t call(const char *tool, const char *arguments)
{
    char body[2048];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",
             tool, arguments);
    return post(body);
}

static void done(reply_t *reply)
{
    cJSON_Delete(reply->json);
    cJSON_Delete(reply->result);
}

static double num(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valuedouble : NAN;
}

static const char *str(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static void test_protocol(void)
{
    reply_t r = post("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-03-26\","
                     "\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"0\"}}}");
    const cJSON *result = cJSON_GetObjectItem(r.json, "result");
    CHECK(num(r.json, "id") == 1);
    CHECK(strcmp(str(result, "protocolVersion"), "2025-03-26") == 0);
    CHECK(strcmp(str(cJSON_GetObjectItem(result, "serverInfo"), "name"), "heating-monitor") == 0);
    CHECK(cJSON_IsObject(cJSON_GetObjectItem(cJSON_GetObjectItem(result, "capabilities"), "tools")));
    CHECK(strlen(str(result, "instructions")) > 100);
    done(&r);

    // A version this server has never heard of gets the one it prefers.
    r = post("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2099-01-01\"}}");
    CHECK(strcmp(str(r.json, "id"), "abc") == 0);
    CHECK(strcmp(str(cJSON_GetObjectItem(r.json, "result"), "protocolVersion"), "2025-06-18") == 0);
    done(&r);

    // A notification is acknowledged and gets no body.
    httpd_req_t req = {0};
    const char *note = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    req.content_len = strlen(note);
    req.body        = note;
    CHECK(s_post(&req) == ESP_OK);
    CHECK(strcmp(req.status, "202 Accepted") == 0 && req.out_len == 0);
    free(req.out);

    r = post("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}");
    CHECK(cJSON_IsObject(cJSON_GetObjectItem(r.json, "result")));
    done(&r);

    r = post("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"resources/list\"}");
    CHECK(num(cJSON_GetObjectItem(r.json, "error"), "code") == -32601);
    done(&r);

    r = post("this is not json");
    CHECK(strcmp(r.status, "400 Bad Request") == 0);
    CHECK(num(cJSON_GetObjectItem(r.json, "error"), "code") == -32700);
    done(&r);

    r = post("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"set_flow_temp\",\"arguments\":{}}}");
    CHECK(num(cJSON_GetObjectItem(r.json, "error"), "code") == -32602);
    done(&r);

    httpd_req_t get = {0};
    CHECK(s_get(&get) == ESP_OK);
    CHECK(strcmp(get.status, "405 Method Not Allowed") == 0);
    free(get.out);
}

static void test_tools_list(void)
{
    reply_t      r     = post("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/list\"}");
    const cJSON *tools = cJSON_GetObjectItem(cJSON_GetObjectItem(r.json, "result"), "tools");
    const cJSON *tool;
    int          read_only = 0;

    CHECK(cJSON_GetArraySize(tools) == 4);
    cJSON_ArrayForEach(tool, tools)
    {
        read_only += cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(tool, "annotations"), "readOnlyHint"));
        CHECK(strcmp(str(cJSON_GetObjectItem(tool, "inputSchema"), "type"), "object") == 0);
        CHECK(strlen(str(tool, "description")) > 50);
    }
    CHECK(read_only == 4);
    CHECK(strcmp(str(cJSON_GetArrayItem(tools, 2), "name"), "summarise_cycles") == 0);
    done(&r);
}

static void test_list_series(void)
{
    reply_t r = call("list_series", "{}");

    CHECK(r.result != NULL && !r.is_error);
    CHECK(num(r.result, "native_interval_seconds") == 5);
    CHECK(strcmp(str(r.result, "earliest"), "2026-01-15T00:00:00+00:00") == 0);
    CHECK(strcmp(str(r.result, "latest"), "2026-01-15T06:00:00+00:00") == 0);

    const cJSON *series = cJSON_GetObjectItem(r.result, "series");
    CHECK(cJSON_GetArraySize(series) == 12);
    CHECK(strcmp(str(cJSON_GetArrayItem(series, 0), "id"), "heat_output_w") == 0);
    CHECK(strcmp(str(cJSON_GetArrayItem(series, 0), "unit"), "W") == 0);
    done(&r);
}

static void test_get_series(void)
{
    // No interval named: at most 1000 points, columnar, ISO timestamps.
    reply_t r = call("get_series", "{\"series\":[\"electrical_input_w\",\"flow_temp_c\"],"
                                   "\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T06:00\"}");
    const cJSON *timestamps = cJSON_GetObjectItem(r.result, "timestamps");
    const cJSON *series     = cJSON_GetObjectItem(r.result, "series");
    int          points     = cJSON_GetArraySize(timestamps);

    CHECK(r.result != NULL && !r.is_error);
    CHECK(points > 0 && points <= 1000);
    CHECK(num(r.result, "interval_seconds") == 25);
    CHECK(strcmp(cJSON_GetArrayItem(timestamps, 0)->valuestring, "2026-01-15T00:00:00+00:00") == 0);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(series, "electrical_input_w")) == points);
    CHECK(cJSON_GetArrayItem(cJSON_GetObjectItem(series, "electrical_input_w"), 0)->valuedouble == 700);
    CHECK(cJSON_GetArrayItem(cJSON_GetObjectItem(series, "flow_temp_c"), 0)->valuedouble == 35);
    done(&r);

    // Past the hard cap: an error naming an interval that fits. Six hours at 4 s is 5400 points.
    r = call("get_series", "{\"series\":[\"electrical_input_w\"],\"start\":\"2026-01-15T00:00\","
                           "\"end\":\"2026-01-15T06:00\",\"interval_seconds\":4}");
    CHECK(r.is_error);
    CHECK(r.text && strstr(r.text, "5000 points") && strstr(r.text, "at least 5"));
    done(&r);

    // Exactly at it: allowed.
    r = call("get_series", "{\"series\":[\"electrical_input_w\"],\"start\":\"2026-01-15T00:00\","
                           "\"end\":\"2026-01-15T06:00\",\"interval_seconds\":5}");
    CHECK(!r.is_error);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(r.result, "timestamps")) == 4320);
    done(&r);

    // Aggregation, UTC output, and a stretch past the end of the data reading as null.
    r = call("get_series", "{\"series\":[\"heat_output_w\"],\"start\":\"2026-01-15T02:00\",\"end\":\"2026-01-15T08:00\","
                           "\"interval_seconds\":3600,\"aggregation\":\"max\",\"timezone\":\"UTC\"}");
    const cJSON *heat = cJSON_GetObjectItem(cJSON_GetObjectItem(r.result, "series"), "heat_output_w");
    CHECK(cJSON_GetArraySize(heat) == 6);
    CHECK(cJSON_GetArrayItem(heat, 0)->valuedouble == 30000);
    CHECK(cJSON_GetArrayItem(heat, 1)->valuedouble == 3500);
    CHECK(cJSON_IsNull(cJSON_GetArrayItem(heat, 4)) && cJSON_IsNull(cJSON_GetArrayItem(heat, 5)));
    CHECK(strcmp(cJSON_GetArrayItem(cJSON_GetObjectItem(r.result, "timestamps"), 0)->valuestring,
                 "2026-01-15T02:00:00Z") == 0);
    done(&r);

    r = call("get_series", "{\"series\":[\"target_flow_c\"],\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T06:00\"}");
    CHECK(r.is_error && strstr(r.text, "Unknown series \"target_flow_c\""));
    done(&r);

    r = call("get_series", "{\"series\":[\"heat_output_w\"],\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T06:00\","
                           "\"timezone\":\"America/New_York\"}");
    CHECK(r.is_error && strstr(r.text, "Europe/London and UTC"));
    done(&r);

    r = call("get_series", "{\"series\":[\"heat_output_w\"],\"start\":\"2026-01-01T00:00\",\"end\":\"2026-03-01T00:00\"}");
    CHECK(r.is_error && strstr(r.text, "longer than 31 days"));
    done(&r);
}

static void test_summarise_cycles(void)
{
    reply_t r = call("summarise_cycles", "{\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T06:00\"}");
    const cJSON *runs    = cJSON_GetObjectItem(r.result, "runs");
    const cJSON *summary = cJSON_GetObjectItem(r.result, "summary");
    const cJSON *dhw     = cJSON_GetArrayItem(runs, 1);
    const cJSON *after   = cJSON_GetArrayItem(runs, 2);

    CHECK(r.result != NULL && !r.is_error);
    CHECK(cJSON_GetArraySize(runs) == 6);

    CHECK(strcmp(str(dhw, "start"), "2026-01-15T01:35:00+00:00") == 0);
    CHECK(strcmp(str(dhw, "end"), "2026-01-15T02:35:00+00:00") == 0);
    CHECK(strcmp(str(dhw, "mode"), "dhw") == 0);
    CHECK(num(dhw, "duration_min") == 60 && num(dhw, "cop") == 2.4 && num(dhw, "avg_heat_w") == 6000);
    CHECK(num(dhw, "flow_at_stop_c") == 52 && num(dhw, "off_time_after_min") == 0);

    // The spike is out, and the changeover is not a compressor start.
    CHECK(strcmp(str(after, "mode"), "heating") == 0);
    CHECK(num(after, "excluded_heat_samples") == 1 && num(after, "max_heat_w") == 3500);
    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(after, "compressor_start")));
    CHECK(cJSON_IsNull(cJSON_GetObjectItem(cJSON_GetArrayItem(runs, 5), "off_time_after_min")));
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetArrayItem(runs, 5), "clipped_by_period")));

    CHECK(num(summary, "runs") == 6 && num(summary, "heating_runs") == 5 && num(summary, "dhw_runs") == 1);
    CHECK(num(summary, "heating_starts_per_hour") == 0.67);
    CHECK(num(summary, "median_heating_run_min") == 35 && num(summary, "median_off_min") == 25);
    CHECK(num(summary, "cop_heating") == 5 && num(summary, "cop_dhw") == 2.4);
    CHECK(num(summary, "dhw_heat_kwh") == 6 && num(summary, "mean_outdoor_c") == 4);
    CHECK(num(summary, "excluded_heat_samples") == 1);
    CHECK(num(cJSON_GetObjectItem(r.result, "thresholds"), "rated_output_w") == 8000);
    CHECK(cJSON_GetObjectItem(r.result, "notes") == NULL);
    done(&r);

    // An override, and the summary alone. A 2 kW threshold leaves only the hot water run.
    r = call("summarise_cycles", "{\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T06:00\","
                                 "\"on_threshold_w\":2000,\"include_runs\":false}");
    CHECK(num(cJSON_GetObjectItem(r.result, "summary"), "runs") == 1);
    CHECK(cJSON_GetObjectItem(r.result, "runs") == NULL);
    done(&r);

    // A day with nothing on the card says so rather than reporting a quiet heat pump.
    r = call("summarise_cycles", "{\"start\":\"2026-02-01T00:00\",\"end\":\"2026-02-02T00:00\"}");
    CHECK(!r.is_error && num(cJSON_GetObjectItem(r.result, "summary"), "runs") == 0);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(r.result, "notes")) == 1);
    done(&r);

    r = call("summarise_cycles", "{\"start\":\"2026-01-15T06:00\",\"end\":\"2026-01-15T00:00\"}");
    CHECK(r.is_error && strstr(r.text, "must be after"));
    done(&r);
}

static void test_compare_periods(void)
{
    reply_t r = call("compare_periods",
                     "{\"period_a\":{\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T01:30\"},"
                     "\"period_b\":{\"start\":\"2026-01-15T03:30\",\"end\":\"2026-01-15T06:00\"}}");
    const cJSON *a = cJSON_GetObjectItem(r.result, "period_a");
    const cJSON *b = cJSON_GetObjectItem(r.result, "period_b");

    CHECK(r.result != NULL && !r.is_error);
    CHECK(num(cJSON_GetObjectItem(a, "summary"), "heating_runs") == 1);
    CHECK(num(cJSON_GetObjectItem(b, "summary"), "heating_runs") == 3);
    CHECK(num(cJSON_GetObjectItem(a, "summary"), "mean_outdoor_c") == 4);
    CHECK(num(cJSON_GetObjectItem(b, "summary"), "mean_outdoor_c") == 4);
    CHECK(strcmp(str(b, "start"), "2026-01-15T03:30:00+00:00") == 0);
    CHECK(cJSON_GetObjectItem(a, "runs") == NULL);
    done(&r);

    r = call("compare_periods", "{\"period_a\":{\"start\":\"2026-01-15T00:00\",\"end\":\"2026-01-15T01:30\"}}");
    CHECK(r.is_error && strstr(r.text, "period_b"));
    done(&r);
}

static void test_no_card(void)
{
    s_card = false;
    reply_t r = call("list_series", "{}");
    CHECK(r.is_error && strstr(r.text, "no SD card"));
    done(&r);
    s_card = true;
}

// `--serve` answers requests from stdin instead of running the tests, one per line -- "GET", or
// "POST " and a body -- writing the status, a tab and the response body. It is there so a real MCP
// client can be pointed at this code through a small HTTP shim, without a board.
static int serve(void)
{
    static char line[16384];

    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';

        httpd_req_t req = {0};
        if (strncmp(line, "POST ", 5) == 0) {
            req.body        = line + 5;
            req.content_len = strlen(req.body);
            s_post(&req);
        } else {
            s_get(&req);
        }

        printf("%s\t%s\n", req.status[0] ? req.status : "200 OK", req.out ? req.out : "");
        fflush(stdout);
        free(req.out);
    }
    return 0;
}

int main(int argc, char **argv)
{
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    write_night();
    CHECK(mcp_server_register(NULL) == ESP_OK);
    CHECK(s_post != NULL && s_get != NULL);

    if (argc > 1 && strcmp(argv[1], "--serve") == 0) {
        return serve();
    }

    test_protocol();
    test_tools_list();
    test_list_series();
    test_get_series();
    test_summarise_cycles();
    test_compare_periods();
    test_no_card();

    remove(FIXTURE_DIR "/home-2026-01-15");
    rmdir(FIXTURE_DIR);

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
