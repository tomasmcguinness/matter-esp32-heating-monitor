#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

// A record of when each Matter node was and was not subscribed, for the Diagnostics page.
//
// The history series record quantities, so a lost subscription shows there only as a gap with no
// cause and no owner. This records the subscriptions themselves: every one established, every one
// that ended, and every attempt that failed, with the CHIP error.
//
// subscription_log_record() is called from the subscription callbacks on the CHIP event loop, so it
// does no I/O -- it stamps the event with the uptime (and the wall time, once SNTP has set it) and
// pushes it onto a RAM ring. The history logger's flush task calls subscription_log_flush() once a
// minute, which writes the ring to /sdcard/subs/YYYY-MM-DD.
//
// File layout: an 8-byte header (magic, version, record size) and then fixed 24-byte records in the
// order they were written. A new day's file opens with SUBLOG_DAY_START and one SUBLOG_UP_AT_START
// per node subscribed at that moment, so a day with no events still says what state it was in.
// SUBLOG_ALIVE is written on every flush, so a reader can tell when the monitor itself was off: the
// last ALIVE before a SUBLOG_BOOT is when it went down.

#define SUBLOG_MAGIC   0x4C425553u // "SUBL"
#define SUBLOG_VERSION 1

typedef enum {
    SUBLOG_BOOT           = 1, // The monitor started; every node is unsubscribed from here.
    SUBLOG_DAY_START      = 2, // First record of a day file; every node is unsubscribed from here...
    SUBLOG_UP_AT_START    = 3, // ...except the ones these name.
    SUBLOG_ESTABLISHED    = 4,
    SUBLOG_TERMINATED     = 5, // An established subscription ended.
    SUBLOG_ATTEMPT_FAILED = 6, // An attempt failed or ended before establishing. error is set when known.
    SUBLOG_REMOVED        = 7, // The node was unpaired.
    SUBLOG_ALIVE          = 8, // Written each flush. node_id is 0.
} sublog_event_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
} sublog_header_t;

typedef struct __attribute__((packed)) {
    uint64_t node_id;
    uint32_t ts;    // Unix seconds
    uint32_t error; // CHIP_ERROR::AsInteger() for SUBLOG_ATTEMPT_FAILED, else 0
    uint8_t  event; // sublog_event_t
    uint8_t  reserved[7];
} sublog_record_t;

#ifdef __cplusplus
static_assert(sizeof(sublog_header_t) == 8, "sublog header is 8 bytes on disk");
static_assert(sizeof(sublog_record_t) == 24, "sublog record is 24 bytes on disk");
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Records SUBLOG_BOOT. Call before the subscription manager starts, so no event precedes it.
esp_err_t subscription_log_init(void);

// Queues an event. Safe from any task; does no I/O.
void subscription_log_record(uint64_t node_id, sublog_event_t event, uint32_t error);

// Writes queued events to the card. Called from the history logger's flush task. Holds them while
// the clock is unset, and drops them (keeping the subscribed set current) when there is no card.
void subscription_log_flush(void);

// GET /api/history/subscriptions?date=YYYY-MM-DD -- the day's events, plus any not yet flushed.
// Routed from history_api.c's wildcard handler, which has already checked the card is mounted.
esp_err_t subscription_log_get_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
