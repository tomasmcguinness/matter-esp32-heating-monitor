#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ISO 8601 in and out for the MCP tools. Unix seconds everywhere inside.
//
// The device has one zone, the one time_sync.c sets in TZ from the time settings, and no tz
// database, so "local" here always means that. A caller in another zone passes timestamps with an explicit
// offset, which are taken as given.

#ifdef __cplusplus
extern "C" {
#endif

// Parses YYYY-MM-DD[THH:MM[:SS[.fff]]] with an optional Z or +-HH[:MM]. With no offset it is
// wall-clock time: UTC if `utc`, otherwise the device's local time.
bool mcp_time_parse(const char *text, bool utc, uint32_t *out);

// Writes 2026-10-08T07:30:00+01:00 in local time, or 2026-10-08T06:30:00Z if `utc`. `buf` needs
// 26 bytes.
void mcp_time_format(uint32_t ts, bool utc, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
