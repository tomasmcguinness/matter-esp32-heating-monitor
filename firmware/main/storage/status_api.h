#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registers GET /api/status. Must be called before the "/*" wildcard handler, which
// would otherwise match first and serve the web app instead.
esp_err_t status_api_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
