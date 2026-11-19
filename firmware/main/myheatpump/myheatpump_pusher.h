#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "managers/home_manager.h"

// Pushes the current heating status to a MyHeatPump dashboard server: POST <base_url>/api/status
// with a JSON body, every interval_s seconds. The body is documented in myheatpump_pusher.cpp.
//
// Only the latest status matters, so unlike the emoncms uploader nothing is buffered: a post that
// fails is logged and forgotten, and the next one goes out at the next interval. Everything
// network-facing happens on the pusher's own task with a 5 s HTTP timeout.
//
// Where the data comes from, with no sensor reading of its own:
//   - home readings: home_manager, read unlocked one field at a time like the history sampler;
//   - room and radiator readings: the history logger's snapshot (history_logger_*_reading);
//   - room and emitter settings: mcp_directory, the same source as the MCP list_rooms tool. It
//     must run on the HTTP task, so it is queued there with httpd_queue_work() and the last copy
//     it returned is used if the HTTP task is busy.

#define MYHEATPUMP_URL_MAX 128
#define MYHEATPUMP_KEY_MAX 129
#define MYHEATPUMP_RESPONSE_MAX 97

#define MYHEATPUMP_INTERVAL_DEFAULT_S 60
#define MYHEATPUMP_INTERVAL_MIN_S     10
#define MYHEATPUMP_INTERVAL_MAX_S     3600

typedef struct
{
    bool enabled;
    // e.g. http://192.168.1.20:5080 -- /api/status is appended. Empty means not pushing.
    char base_url[MYHEATPUMP_URL_MAX];
    // Sent as X-Api-Key, and only when set. Never log it or put it in a response;
    // myheatpump_pusher_get_config() blanks it.
    char api_key[MYHEATPUMP_KEY_MAX];
    uint16_t interval_s;
} myheatpump_config_t;

typedef struct
{
    uint32_t last_success_ts; // 0 until a post has succeeded since boot
    uint32_t last_error_ts;   // 0 until one has failed
    int last_error_http_status; // 0 when the request never got an answer
    char last_error[MYHEATPUMP_RESPONSE_MAX];
    uint32_t posts_ok;
    uint32_t posts_failed;
    uint32_t last_payload_bytes;
} myheatpump_status_t;

// Loads the configuration from NVS and starts the task. `get_server` returns the web server's
// handle, or NULL while it is not running; it is called from the pusher task. Safe to call before
// the network is up.
esp_err_t myheatpump_pusher_init(home_manager_t *home_manager, httpd_handle_t (*get_server)(void));

// The current configuration with `api_key` blanked. `key_set` says whether one is stored.
void myheatpump_pusher_get_config(myheatpump_config_t *config, bool *key_set);

// Saves to NVS and applies straight away. An empty `api_key` keeps the stored one unless
// `clear_key` is set. ESP_ERR_INVALID_ARG for a URL that is neither empty nor http(s)://, or an
// interval out of range.
esp_err_t myheatpump_pusher_set_config(const myheatpump_config_t *config, bool clear_key);

void myheatpump_pusher_get_status(myheatpump_status_t *status);
