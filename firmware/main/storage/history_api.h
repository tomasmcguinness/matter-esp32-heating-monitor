#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// Registers the history endpoints:
//
//   GET /api/history?date=YYYY-MM-DD&points=N
//        the home's recorded values for a day, plus energyWh and COP
//   GET /api/history/dates
//        which days have data on the card
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
