#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include "managers/node_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registers the /api/companion/* endpoints. Must be called before the "/*" wildcard handler,
// which would otherwise match first and serve the web app instead.
//
// This is the API of the generic Matter Controller Companion app
// (https://github.com/tomasmcguinness/matter-controller-companion-app), which works against any
// self-hosted controller. It is a published contract rather than ours to shape, so the paths,
// bodies and status codes here follow that repo's README and must not be changed to suit this
// firmware -- change the app first.
//
// The Matter *operations* are shared with the web UI's /api/nodes endpoints through node_api.h,
// so both drive the same commissioning, unpair and rename code. The **payloads are not shared**:
// this file builds its own node array against the contract above, and /api/nodes builds its own
// for the web UI. That separation is the reason for the /companion/ namespace -- neither side's
// needs may leak into the other's response.
esp_err_t companion_api_register(httpd_handle_t server, node_manager_t *node_manager);

#ifdef __cplusplus
}
#endif
