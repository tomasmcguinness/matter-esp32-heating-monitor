#include "emoncms_uploader.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "storage/time_sync.h"

static const char *TAG = "emoncms";

#define NVS_NAMESPACE "emoncms"

#define DEFAULT_BASE_URL "https://emoncms.org"
#define DEFAULT_NODE     "heating_monitor"

// The TLS handshake runs on this task's stack.
#define TASK_STACK    8192
#define TASK_PRIORITY 3

#define HTTP_TIMEOUT_MS 10000

// How much history the buffer holds while posts are failing, and the most entries that can be
// at the shortest interval. The array is allocated once at that size; the configured interval
// only decides how much of it is used.
#define BUFFER_SPAN_S   3600
#define BUFFER_MAX      (BUFFER_SPAN_S / EMONCMS_INTERVAL_MIN_S)

// A failed post is retried after 5 s, doubling to a minute. The cap is short because the
// buffer only covers an hour: the sooner a returning network is noticed, the less is dropped.
#define BACKOFF_MIN_MS 5000
#define BACKOFF_MAX_MS 60000

// The gap between posts while a backlog is being sent, so an hour of readings does not arrive at
// the server as one burst.
#define BACKFILL_PACE_MS 250

#define NOTIFY_SAMPLE (1u << 0)
#define NOTIFY_CONFIG (1u << 1)
#define NOTIFY_TEST   (1u << 2)

// ---------------------------------------------------------------------------
// What is sent
// ---------------------------------------------------------------------------

// One emoncms input: its key, how to read it from home_manager, and how many decimals it is
// printed with. `read` returns false when there is no reading, and the key is then left out of
// the payload altogether -- emoncms has no null, and a placeholder would be charted as a value.
//
// Each reader tests its has_* flag and takes the value once, into a local, for the reason
// history_logger's build_record() gives: these fields are written from the CHIP event loop and
// read here unlocked.
//
// The conversions are the ones calculations_manager's MQTT home payload uses -- temperatures are
// 0.01 degC, power is mW. Flow is the exception: the heat meter reports litres per hour, which is
// what MQTT and the history record carry, and emoncms is sent litres per minute.
typedef struct
{
    const char *key;
    bool (*read)(const home_manager_t *h, double *value);
    uint8_t decimals;
} emoncms_input_t;

static const emoncms_input_t INPUTS[] = {
    {"flow_rate", [](const home_manager_t *h, double *v) {
         if (!h->has_heat_meter_flow) return false;
         *v = (double)h->heat_meter_flow / 60.0;
         return true;
     }, 2},
    {"flow_temp", [](const home_manager_t *h, double *v) {
         if (!h->has_heat_meter_flow_temperature) return false;
         *v = (double)h->heat_meter_flow_temperature / 100.0;
         return true;
     }, 2},
    {"return_temp", [](const home_manager_t *h, double *v) {
         if (!h->has_heat_meter_return_temperature) return false;
         *v = (double)h->heat_meter_return_temperature / 100.0;
         return true;
     }, 2},
    {"internal_temp", [](const home_manager_t *h, double *v) {
         if (!h->has_average_internal_temperature) return false;
         *v = (double)h->average_internal_temperature / 100.0;
         return true;
     }, 2},
    {"outdoor_temp", [](const home_manager_t *h, double *v) {
         if (!h->has_outdoor_temperature) return false;
         *v = (double)h->outdoor_temperature / 100.0;
         return true;
     }, 2},
    {"elec_in", [](const home_manager_t *h, double *v) {
         if (!h->has_electrical_power) return false;
         int64_t mw = h->electrical_power_mw;
         *v = (double)mw / 1000.0;
         return true;
     }, 1},
    {"heat_out", [](const home_manager_t *h, double *v) {
         if (!h->has_heat_meter_power) return false;
         int64_t mw = h->heat_meter_power_mw;
         *v = (double)mw / 1000.0;
         return true;
     }, 1},
    {"heating_running", [](const home_manager_t *h, double *v) {
         if (!h->has_heating_running) return false;
         *v = h->heating_running ? 1 : 0;
         return true;
     }, 0},
    {"dhw_running", [](const home_manager_t *h, double *v) {
         if (!h->has_dhw_running) return false;
         *v = h->dhw_running ? 1 : 0;
         return true;
     }, 0},
};

#define INPUT_COUNT (sizeof(INPUTS) / sizeof(INPUTS[0]))

size_t emoncms_uploader_input_count(void)
{
    return INPUT_COUNT;
}

const char *emoncms_uploader_input_key(size_t index)
{
    return index < INPUT_COUNT ? INPUTS[index].key : NULL;
}

// Writes the readings that exist as a JSON object. Returns how many there were; with none, the
// caller has nothing to post.
static size_t build_payload(const home_manager_t *h, char *out, size_t len)
{
    size_t used = 0;
    size_t count = 0;

    out[used++] = '{';

    for (size_t i = 0; i < INPUT_COUNT; i++)
    {
        double value;

        if (!INPUTS[i].read(h, &value))
        {
            continue;
        }

        int n = snprintf(out + used, len - used, "%s\"%s\":%.*f", count ? "," : "", INPUTS[i].key,
                         INPUTS[i].decimals, value);

        // Leave room for the closing brace and the NUL.
        if (n < 0 || used + n + 2 > len)
        {
            break;
        }

        used += n;
        count++;
    }

    out[used++] = '}';
    out[used] = '\0';

    return count;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct entry_t
{
    uint32_t ts;
    char payload[EMONCMS_PAYLOAD_MAX];
};

static home_manager_t *s_home_manager;
static TaskHandle_t s_task;
static esp_timer_handle_t s_timer;

// Guards s_config and s_status, which the HTTP task reads and writes. The buffer needs no lock:
// only the uploader task touches it.
static SemaphoreHandle_t s_mutex;
static emoncms_config_t s_config;
static emoncms_status_t s_status;

// Ring buffer of posts waiting to be sent, in PSRAM. s_head is the oldest.
static entry_t *s_buffer;
static size_t s_head;
static size_t s_count;
static size_t s_capacity = 1;

static int64_t s_next_attempt_us;
static uint32_t s_backoff_ms;

// The connection is kept between posts so a backlog does not pay for a TLS handshake each time.
// s_response is where the event handler collects the body of the request in flight.
static esp_http_client_handle_t s_client;
static char s_response[EMONCMS_RESPONSE_MAX];
static size_t s_response_len;

// One test at a time: the caller holds s_test_lock, the task fills s_test_result and gives
// s_test_done.
static SemaphoreHandle_t s_test_lock;
static SemaphoreHandle_t s_test_done;
static emoncms_result_t s_test_result;
static esp_err_t s_test_err;

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

static void config_defaults(emoncms_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->enabled = false;
    strlcpy(c->base_url, DEFAULT_BASE_URL, sizeof(c->base_url));
    strlcpy(c->node, DEFAULT_NODE, sizeof(c->node));
    c->interval_s = EMONCMS_INTERVAL_DEFAULT_S;
}

static void load_config(emoncms_config_t *c)
{
    config_defaults(c);

    nvs_handle_t nvs;

    // Nothing saved yet is the normal first-boot case, and the defaults stand.
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

    len = sizeof(c->node);
    nvs_get_str(nvs, "node", c->node, &len);

    uint16_t interval = 0;
    if (nvs_get_u16(nvs, "interval_s", &interval) == ESP_OK && interval >= EMONCMS_INTERVAL_MIN_S &&
        interval <= EMONCMS_INTERVAL_MAX_S)
    {
        c->interval_s = interval;
    }

    nvs_close(nvs);
}

static esp_err_t save_config(const emoncms_config_t *c)
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
    if (err == ESP_OK) err = nvs_set_str(nvs, "node", c->node);
    if (err == ESP_OK) err = nvs_set_u16(nvs, "interval_s", c->interval_s);
    if (err == ESP_OK) err = nvs_commit(nvs);

    nvs_close(nvs);

    return err;
}

// ---------------------------------------------------------------------------
// Posting
// ---------------------------------------------------------------------------

// Percent-encodes everything outside the unreserved set. Returns false if it did not fit.
static bool url_encode(const char *in, char *out, size_t len)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t used = 0;

    for (; *in; in++)
    {
        unsigned char c = (unsigned char)*in;
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~';

        if (used + (plain ? 1 : 3) + 1 > len)
        {
            return false;
        }

        if (plain)
        {
            out[used++] = (char)c;
        }
        else
        {
            out[used++] = '%';
            out[used++] = HEX[c >> 4];
            out[used++] = HEX[c & 0x0f];
        }
    }

    out[used] = '\0';

    return true;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0)
    {
        // Only the start of the body is kept. It is there to be shown to the user, and a success
        // is a dozen bytes.
        size_t room = sizeof(s_response) - 1 - s_response_len;
        size_t take = (size_t)event->data_len < room ? (size_t)event->data_len : room;

        memcpy(s_response + s_response_len, event->data, take);
        s_response_len += take;
        s_response[s_response_len] = '\0';
    }

    return ESP_OK;
}

static void close_client(void)
{
    if (s_client)
    {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
}

// Sends one payload. Fills `result` whatever happens. Only {"success": true} counts: emoncms
// answers some rejected posts with a 200 and a message.
static void post_payload(const emoncms_config_t *config, uint32_t ts, const char *payload, emoncms_result_t *result)
{
    // Static because the task's stack is better spent on TLS, and only the task calls this.
    static char url[EMONCMS_URL_MAX + 16];
    static char auth[EMONCMS_KEY_MAX + 8];
    static char encoded_node[EMONCMS_NODE_MAX * 3];
    static char encoded_payload[EMONCMS_PAYLOAD_MAX * 3];
    static char body[sizeof(encoded_node) + sizeof(encoded_payload) + 48];

    memset(result, 0, sizeof(*result));

    if (!url_encode(config->node, encoded_node, sizeof(encoded_node)) ||
        !url_encode(payload, encoded_payload, sizeof(encoded_payload)))
    {
        strlcpy(result->response, "Payload too long to encode", sizeof(result->response));
        return;
    }

    int body_len = snprintf(body, sizeof(body), "node=%s&fulljson=%s&time=%lu", encoded_node, encoded_payload,
                            (unsigned long)ts);

    if (!s_client)
    {
        size_t base_len = strlen(config->base_url);

        while (base_len > 0 && config->base_url[base_len - 1] == '/')
        {
            base_len--;
        }

        snprintf(url, sizeof(url), "%.*s/input/post", (int)base_len, config->base_url);
        snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);

        esp_http_client_config_t http = {};
        http.url = url;
        http.method = HTTP_METHOD_POST;
        http.timeout_ms = HTTP_TIMEOUT_MS;
        http.event_handler = http_event;
        http.crt_bundle_attach = esp_crt_bundle_attach;
        http.keep_alive_enable = true;

        s_client = esp_http_client_init(&http);

        if (!s_client)
        {
            strlcpy(result->response, "Could not create the HTTP client", sizeof(result->response));
            return;
        }

        esp_http_client_set_header(s_client, "Authorization", auth);
        esp_http_client_set_header(s_client, "Content-Type", "application/x-www-form-urlencoded");
    }

    s_response_len = 0;
    s_response[0] = '\0';

    esp_http_client_set_post_field(s_client, body, body_len);

    esp_err_t err = esp_http_client_perform(s_client);

    if (err != ESP_OK)
    {
        // The connection is in an unknown state, so the next post starts a new one.
        close_client();
        strlcpy(result->response, esp_err_to_name(err), sizeof(result->response));
        return;
    }

    result->http_status = esp_http_client_get_status_code(s_client);
    strlcpy(result->response, s_response, sizeof(result->response));

    if (result->http_status == 200)
    {
        cJSON *json = cJSON_Parse(s_response);

        result->ok = cJSON_IsTrue(cJSON_GetObjectItem(json, "success"));

        cJSON_Delete(json);
    }

    if (!result->ok)
    {
        close_client();
    }
}

// Sends a payload and records the outcome in the status the UI reads.
static void post_and_record(const emoncms_config_t *config, uint32_t ts, const char *payload, emoncms_result_t *result)
{
    post_payload(config, ts, payload, result);

    uint32_t now = (uint32_t)time(NULL);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_status.last_payload_ts = ts;
    strlcpy(s_status.last_payload, payload, sizeof(s_status.last_payload));

    if (result->ok)
    {
        s_status.posts_ok++;
        s_status.last_success_ts = now;
    }
    else
    {
        s_status.posts_failed++;
        s_status.last_error_ts = now;
        s_status.last_error = *result;
    }

    xSemaphoreGive(s_mutex);

    if (!result->ok)
    {
        // The status and nothing else. The body is on the Settings page; it stays out of the log.
        ESP_LOGW(TAG, "Post failed (HTTP %d)", result->http_status);
    }
}

// ---------------------------------------------------------------------------
// The buffer
// ---------------------------------------------------------------------------

static void publish_buffer_state(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.buffered = (uint16_t)s_count;
    s_status.buffer_capacity = (uint16_t)s_capacity;
    xSemaphoreGive(s_mutex);
}

static void drop_oldest(void)
{
    s_head = (s_head + 1) % BUFFER_MAX;
    s_count--;
}

static void push(uint32_t ts, const char *payload)
{
    uint32_t dropped = 0;

    while (s_count >= s_capacity)
    {
        drop_oldest();
        dropped++;
    }

    entry_t *entry = &s_buffer[(s_head + s_count) % BUFFER_MAX];

    entry->ts = ts;
    strlcpy(entry->payload, payload, sizeof(entry->payload));
    s_count++;

    if (dropped)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.dropped += dropped;
        xSemaphoreGive(s_mutex);
    }
}

// ---------------------------------------------------------------------------
// The task
// ---------------------------------------------------------------------------

static void timer_cb(void *arg)
{
    xTaskNotify(s_task, NOTIFY_SAMPLE, eSetBits);
}

static bool can_post(const emoncms_config_t *config)
{
    return config->enabled && config->api_key[0] != '\0' && config->base_url[0] != '\0';
}

// Picks up a configuration change: the timer's period, how much of the buffer is in use, and a
// connection that may now point at the wrong server or carry the wrong key.
static void apply_config(const emoncms_config_t *config)
{
    esp_timer_stop(s_timer);
    close_client();

    s_capacity = BUFFER_SPAN_S / config->interval_s;

    if (s_capacity < 1)
    {
        s_capacity = 1;
    }

    if (s_capacity > BUFFER_MAX)
    {
        s_capacity = BUFFER_MAX;
    }

    if (!can_post(config))
    {
        // Readings queued for a server that is no longer being posted to are not worth keeping.
        s_head = 0;
        s_count = 0;
    }
    else
    {
        while (s_count > s_capacity)
        {
            drop_oldest();
        }

        esp_timer_start_periodic(s_timer, (uint64_t)config->interval_s * 1000000ULL);
    }

    s_backoff_ms = 0;
    s_next_attempt_us = 0;

    publish_buffer_state();

    ESP_LOGI(TAG, "%s, every %u s, node \"%s\"", can_post(config) ? "Uploading" : "Not uploading",
             config->interval_s, config->node);
}

static void take_sample(void)
{
    // Before the first SNTP sync time(NULL) is in 1970, and emoncms would file the reading there.
    if (!clock_is_valid())
    {
        return;
    }

    char payload[EMONCMS_PAYLOAD_MAX];

    if (build_payload(s_home_manager, payload, sizeof(payload)) == 0)
    {
        return;
    }

    push((uint32_t)time(NULL), payload);
    publish_buffer_state();
}

static void run_test(const emoncms_config_t *config)
{
    memset(&s_test_result, 0, sizeof(s_test_result));
    s_test_err = ESP_ERR_INVALID_STATE;

    char payload[EMONCMS_PAYLOAD_MAX];

    if (config->api_key[0] == '\0')
    {
        strlcpy(s_test_result.response, "No API key has been saved", sizeof(s_test_result.response));
    }
    else if (!clock_is_valid())
    {
        strlcpy(s_test_result.response, "The clock has not been set yet", sizeof(s_test_result.response));
    }
    else if (build_payload(s_home_manager, payload, sizeof(payload)) == 0)
    {
        strlcpy(s_test_result.response, "There are no readings to send", sizeof(s_test_result.response));
    }
    else
    {
        post_and_record(config, (uint32_t)time(NULL), payload, &s_test_result);
        s_test_err = ESP_OK;
    }

    xSemaphoreGive(s_test_done);
}

static void uploader_task(void *arg)
{
    emoncms_config_t config;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    config = s_config;
    xSemaphoreGive(s_mutex);

    apply_config(&config);

    for (;;)
    {
        // Sleep until something happens, or until the queue is next due.
        TickType_t wait = portMAX_DELAY;

        if (s_count > 0 && can_post(&config))
        {
            int64_t due_us = s_next_attempt_us - esp_timer_get_time();
            wait = due_us > 0 ? pdMS_TO_TICKS(due_us / 1000) + 1 : 0;
        }

        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, wait);

        if (bits & NOTIFY_CONFIG)
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            config = s_config;
            xSemaphoreGive(s_mutex);

            apply_config(&config);
        }

        if (bits & NOTIFY_TEST)
        {
            run_test(&config);
        }

        if ((bits & NOTIFY_SAMPLE) && can_post(&config))
        {
            take_sample();
        }

        // One post per pass, oldest first, so a long backlog never keeps a test or a
        // configuration change waiting behind it.
        if (s_count == 0 || !can_post(&config) || esp_timer_get_time() < s_next_attempt_us)
        {
            continue;
        }

        entry_t *oldest = &s_buffer[s_head];
        emoncms_result_t result;

        post_and_record(&config, oldest->ts, oldest->payload, &result);

        if (result.ok)
        {
            drop_oldest();
            s_backoff_ms = 0;
            s_next_attempt_us = esp_timer_get_time() + (s_count > 0 ? BACKFILL_PACE_MS * 1000LL : 0);
        }
        else
        {
            s_backoff_ms = s_backoff_ms == 0 ? BACKOFF_MIN_MS : s_backoff_ms * 2;

            if (s_backoff_ms > BACKOFF_MAX_MS)
            {
                s_backoff_ms = BACKOFF_MAX_MS;
            }

            s_next_attempt_us = esp_timer_get_time() + (int64_t)s_backoff_ms * 1000LL;
        }

        publish_buffer_state();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t emoncms_uploader_init(home_manager_t *home_manager)
{
    if (s_task)
    {
        return ESP_OK;
    }

    s_home_manager = home_manager;

    s_mutex = xSemaphoreCreateMutex();
    s_test_lock = xSemaphoreCreateMutex();
    s_test_done = xSemaphoreCreateBinary();
    s_buffer = (entry_t *)heap_caps_malloc(BUFFER_MAX * sizeof(entry_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!s_mutex || !s_test_lock || !s_test_done || !s_buffer)
    {
        ESP_LOGE(TAG, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    load_config(&s_config);

    const esp_timer_create_args_t timer_args = {
        .callback = timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emoncms",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_timer);

    if (err != ESP_OK)
    {
        return err;
    }

    if (xTaskCreate(uploader_task, "emoncms", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS)
    {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void emoncms_uploader_get_config(emoncms_config_t *config, bool *key_set)
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

esp_err_t emoncms_uploader_set_config(const emoncms_config_t *config)
{
    bool http = strncmp(config->base_url, "http://", 7) == 0;
    bool https = strncmp(config->base_url, "https://", 8) == 0;

    if ((!http && !https) || config->node[0] == '\0' || config->interval_s < EMONCMS_INTERVAL_MIN_S ||
        config->interval_s > EMONCMS_INTERVAL_MAX_S)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    emoncms_config_t updated = *config;

    if (updated.api_key[0] == '\0')
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

void emoncms_uploader_get_status(emoncms_status_t *status)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_mutex);
}

esp_err_t emoncms_uploader_test(emoncms_result_t *result)
{
    memset(result, 0, sizeof(*result));

    if (!s_task)
    {
        strlcpy(result->response, "The uploader is not running", sizeof(result->response));
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_test_lock, portMAX_DELAY);

    // Clear a completion left behind by a test that was given up on.
    xSemaphoreTake(s_test_done, 0);
    xTaskNotify(s_task, NOTIFY_TEST, eSetBits);

    esp_err_t err;

    // The task may be part-way through a post of its own when asked, so allow for two timeouts.
    if (xSemaphoreTake(s_test_done, pdMS_TO_TICKS(2 * HTTP_TIMEOUT_MS + 5000)) == pdTRUE)
    {
        *result = s_test_result;
        err = s_test_err;
    }
    else
    {
        strlcpy(result->response, "Timed out waiting for the uploader", sizeof(result->response));
        err = ESP_ERR_TIMEOUT;
    }

    xSemaphoreGive(s_test_lock);

    return err;
}
