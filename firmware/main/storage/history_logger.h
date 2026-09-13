#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "managers/home_manager.h"

// Samples the home's tracked values on a fixed cadence and appends fixed-width records to
// the SD card. See history_format.h for the on-disk layout.
//
// Values are read from home_manager rather than from individual sensors, so the recorded
// series follows the QUANTITY and survives a sensor being replaced or re-paired.
//
// Sampling is decoupled from Matter reporting on purpose: the sampler polls on its own
// timer rather than logging on receipt, so a stalled or bursty subscription cannot leave
// gaps in an otherwise evenly-spaced record, and no SD I/O happens on the CHIP event loop.

#define HISTORY_INTERVAL_DEFAULT_S 5
#define HISTORY_INTERVAL_MIN_S     1
#define HISTORY_INTERVAL_MAX_S     3600

#ifdef __cplusplus
extern "C" {
#endif

// Starts the sample and flush timers. The manager must outlive the logger; it is one of
// the app's globals. Safe to call when no card is mounted -- the timers still run but
// every tick is a no-op, so plugging a card in and rebooting is all that is needed.
esp_err_t history_logger_init(home_manager_t *home_manager);

// The requested interval. A change waits for the next local midnight, because a file's
// records must stay spaced at the interval its header records -- that is what makes
// offset = header + slot * record_size correct. history_logger_active_interval() is what
// today's files are actually being written at.
void     history_logger_set_interval(uint16_t interval_s);
uint16_t history_logger_interval(void);
uint16_t history_logger_active_interval(void);

#ifdef __cplusplus
}
#endif
