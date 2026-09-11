#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts SNTP. Call once the network is up (IP_EVENT_ETH_GOT_IP); repeat calls are
// ignored. Also sets TZ, so localtime_r() gives UK local time -- history files are
// named and bucketed by local day so that a "day" on the dashboard is a day in the
// house.
void time_sync_start(void);

// False until SNTP has set the clock at least once. Before that time(NULL) is somewhere
// in 1970 and any dated filename would be wrong, so the sampler skips its ticks.
bool clock_is_valid(void);

// Unix time of local midnight for the local day containing ts.
uint32_t local_midnight_for(uint32_t ts);

// Formats the local date of ts as YYYY-MM-DD. buf must hold at least 11 bytes.
void local_date_string(uint32_t ts, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
