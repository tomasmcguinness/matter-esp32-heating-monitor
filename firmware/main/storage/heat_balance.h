#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "managers/home_manager.h"
#include "heat_balance_api.h"

// The heat balance (TASK-45): every 10 minutes of local clock time, the heat meter's energy against
// the live model's radiator and UFH energy, counted only while space heating. See heat_balance_core.h
// for what counts.
//
// Unlike the rest of the history this records what the live model *said*, with the calibration
// points and radiator ratings then in force, and it is never recalculated. That is deliberate: it
// shows whether the figures on screen matched the meter at the time.
//
// Files are /sdcard/balance/YYYY-MM-DD: an 8-byte header (BALN, version 2, record size, bucket
// seconds) and one 32-byte hb_record_t per completed bucket, in order. Version 1 (a few hours of
// 2026-11-24) was hourly, with 0 for the bucket length.

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t heat_balance_init(void);

// From the history sampler, on its esp_timer task, once per sample of `seconds`. Reads home_manager
// unlocked, one field at a time, as the sampler's build_record() does. No file I/O.
void heat_balance_sample(const home_manager_t *home, uint32_t now, uint32_t seconds);

// From the history flush task: appends any completed buckets to their day's file.
void heat_balance_flush(void);

#ifdef __cplusplus
}
#endif
