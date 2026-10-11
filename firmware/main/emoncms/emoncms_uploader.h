#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "managers/home_manager.h"

// Posts the home's readings to an emoncms server (emoncms.org by default) over HTTPS.
//
// Home-level readings only, and a fixed set of them -- see the INPUTS table in the .cpp. Every
// input becomes a feed, and emoncms.org charges per feed, so rooms, radiators and COP are left
// out on purpose.
//
// Everything network-facing happens on the uploader's own task. A timer wakes it at the configured
// interval; it reads home_manager, queues a post and sends the queue oldest first. A post that
// fails stays queued in PSRAM with the timestamp it was taken at and is retried with backoff, so a
// network outage of up to about an hour is backfilled rather than lost.

#define EMONCMS_URL_MAX      128
#define EMONCMS_KEY_MAX      65
#define EMONCMS_NODE_MAX     33
#define EMONCMS_PAYLOAD_MAX  256
#define EMONCMS_RESPONSE_MAX 161

#define EMONCMS_INTERVAL_DEFAULT_S 60
#define EMONCMS_INTERVAL_MIN_S     10
#define EMONCMS_INTERVAL_MAX_S     3600

typedef struct
{
    bool enabled;
    char base_url[EMONCMS_URL_MAX];
    // The write API key. It goes out in the Authorization header and nowhere else: never log it
    // and never put it in a response. emoncms_uploader_get_config() blanks it for that reason.
    char api_key[EMONCMS_KEY_MAX];
    char node[EMONCMS_NODE_MAX];
    uint16_t interval_s;
} emoncms_config_t;

// The outcome of one post. `http_status` is 0 when the request never got an answer, and
// `response` then names the transport error instead of holding a body.
typedef struct
{
    bool ok;
    int http_status;
    char response[EMONCMS_RESPONSE_MAX];
} emoncms_result_t;

typedef struct
{
    uint32_t last_success_ts; // 0 until a post has succeeded since boot
    uint32_t last_error_ts;   // 0 until one has failed
    emoncms_result_t last_error;
    uint32_t posts_ok;
    uint32_t posts_failed;
    uint32_t dropped; // posts discarded because the buffer was full
    uint16_t buffered;
    uint16_t buffer_capacity;
    uint32_t last_payload_ts; // the timestamp the last payload was sent with
    char last_payload[EMONCMS_PAYLOAD_MAX];
} emoncms_status_t;

// Loads the configuration from NVS and starts the task. The manager must outlive the uploader; it
// is one of the app's globals. Safe to call before the network is up.
esp_err_t emoncms_uploader_init(home_manager_t *home_manager);

// The current configuration, with `api_key` blanked. `key_set` says whether there is one.
void emoncms_uploader_get_config(emoncms_config_t *config, bool *key_set);

// Saves the configuration to NVS and applies it straight away. An empty `api_key` keeps the one
// already stored. ESP_ERR_INVALID_ARG if the URL, node or interval is unusable.
esp_err_t emoncms_uploader_set_config(const emoncms_config_t *config);

void emoncms_uploader_get_status(emoncms_status_t *status);

// The emoncms keys that can appear in a payload, in the order they are sent. The UI lists these so
// it can show which ones the last payload was missing.
size_t emoncms_uploader_input_count(void);
const char *emoncms_uploader_input_key(size_t index);

// Sends the current readings once, whether or not uploading is enabled, and waits for the answer.
// This blocks the caller for as long as the request takes -- up to the HTTP timeout -- so it is
// for a button the user presses, not for anything periodic. ESP_ERR_INVALID_STATE with a reason in
// `result->response` when there is nothing that could be sent.
esp_err_t emoncms_uploader_test(emoncms_result_t *result);
