#pragma once

#include <stddef.h>

// The time zones the device can be set to. There is no tz database on the board, so each is an
// IANA name -- what the user picks and what the MCP tools accept -- paired with the POSIX TZ
// string that newlib actually understands. A POSIX string carries one year's rule, so a country
// that changes its clock rules needs its entry updated here.

#ifdef __cplusplus
extern "C" {
#endif

#define TIMEZONE_DEFAULT  "Europe/London"
#define TIMEZONE_NAME_MAX 48

typedef struct {
    const char *name;  // IANA, e.g. "Europe/London"
    const char *posix; // e.g. "GMT0BST,M3.5.0/1,M10.5.0"
} tz_entry_t;

// The POSIX TZ string for an IANA name, or NULL if the name is not in the table.
const char *timezones_posix_for(const char *name);

size_t            timezones_count(void);
const tz_entry_t *timezones_at(size_t index);

#ifdef __cplusplus
}
#endif
