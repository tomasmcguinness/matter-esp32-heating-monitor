#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// The node *operations*, factored out of the HTTP handlers in app_main.cpp so the web UI's
// /api/nodes and the companion app's /api/companion/nodes drive the same Matter code.
//
// Operations only. **Payload shapes are not shared**: /api/nodes feeds our own web UI and changes
// whenever the UI needs it to, while /api/companion/* is a published contract owned by someone
// else (see companion_api.h). Each builds its own JSON, which is the whole point of the separate
// namespace -- a field added for the UI must not appear on the contract, and the contract must not
// constrain the UI. The web UI's node array is built by app_main.cpp's build_nodes_json(); the
// companion app's by companion_api.cpp's build_companion_nodes_json().

// Everything the caller needs to answer a commissioning request. `message` is sent as the body of
// a non-2xx, and the companion app shows it to the user, so keep it readable.
typedef struct
{
    uint64_t node_id;
    int http_status; // 201, 400, 409, 500, 502 or 504
    char message[128];
} commission_outcome_t;

// Commissions `setup_code` onto the fabric and blocks until pairing finishes or times out --
// this parks the httpd task, so nothing else is served meanwhile. Always fills `outcome`; the
// esp_err_t is ESP_OK whenever `outcome` was filled, whether or not pairing worked.
esp_err_t commission_node(const char *setup_code, commission_outcome_t *outcome);

// Writes `outcome` to the response: 201 and {"nodeId": N} when it succeeded, otherwise its status
// with `message` as the body.
esp_err_t send_commission_outcome(httpd_req_t *req, const commission_outcome_t *outcome);

// Removes the node from the fabric and from the node manager. Does not wait for the unpair
// callback.
esp_err_t unpair_node(uint64_t node_id);

// Sets the node's user-visible name and persists it. ESP_ERR_NOT_FOUND if there is no such node.
esp_err_t rename_node(uint64_t node_id, const char *name);

// Reads the request body and parses it as JSON. Sends a 400 and returns NULL if the body could not
// be read or is not JSON, so the caller can simply return ESP_OK. The caller owns the result.
cJSON *read_json_body(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
