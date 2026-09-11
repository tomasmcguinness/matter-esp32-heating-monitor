#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// Registers the history endpoints:
//
//   GET /api/history?date=YYYY-MM-DD&points=N
//        the resolved home series for a day, plus energyWh and COP
//   GET /api/history/sensors
//        which days and which sensors have data on the card
//   GET /api/history/sensor?node=<id>&endpoint=<id>&date=YYYY-MM-DD&points=N
//        one raw sensor series
//
// Two handlers are used: an exact match on /api/history and a wildcard on
// /api/history/*.

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t history_api_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
