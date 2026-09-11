#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the MicroSD card. Safe to call when no card is fitted: it logs and returns
// an error, and sd_card_available() stays false, which the history logger and the
// history endpoints treat as "no storage" rather than a failure.
esp_err_t sd_card_init(void);

// True once a card is mounted. Everything that touches /sdcard checks this first.
bool sd_card_available(void);

#ifdef __cplusplus
}
#endif
