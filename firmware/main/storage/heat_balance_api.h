#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// The heat balance's endpoint (see heat_balance.h), in a header of its own because
// history_api.c is C and heat_balance.h pulls in home_manager.h, which is C++.

#ifdef __cplusplus
extern "C" {
#endif

// GET /api/history/balance?date=YYYY-MM-DD: {storage, date, bucketSeconds, buckets: [{start,
// countedSeconds, unknownSeconds, heatMeterWh, radiatorsWh, ufhWh}], current: {...} | null}, where
// current is the bucket in progress when the date is today. Routed from the /api/history/* wildcard
// in history_api.c.
esp_err_t heat_balance_get_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
