#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// A read-only MCP server over the recorded home history, so an AI assistant can analyse cycling,
// COP and the effect of heating-curve changes without the data leaving the board first.
//
//   POST /mcp   Streamable HTTP transport, stateless, one JSON response per request
//   GET  /mcp   405 -- there are no sessions, so no stream to open
//
// Tools: list_series, get_series, summarise_cycles, compare_periods. Every one only reads the
// day files on the SD card. Nothing here writes to the card, changes any manager, or can reach
// the heat pump.
//
// There is no authentication, like the rest of this device's HTTP API: anyone who can reach
// port 80 can read the history. Do not forward this port to the internet.
//
// Two URI handlers. Must be registered before the "/*" wildcard.

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mcp_server_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
