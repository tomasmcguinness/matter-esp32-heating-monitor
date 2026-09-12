#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

#ifdef __cplusplus
extern "C" {
#endif

// A card that is fitted but unreadable looks identical from the outside to no card at
// all, and the two need very different things done about them, so they are reported
// separately rather than collapsed into "unavailable".
typedef enum {
    SD_CARD_ABSENT,         // Nothing in the slot, or the card never answered.
    SD_CARD_NO_FILESYSTEM,  // Card initialised, but carries no mountable FAT volume.
    SD_CARD_MOUNTED,        // Mounted at SD_CARD_MOUNT_POINT and writable.
} sd_card_state_t;

// Mounts the MicroSD card. Safe to call when no card is fitted: it logs and returns
// an error, and sd_card_available() stays false, which the history logger and the
// history endpoints treat as "no storage" rather than a failure.
esp_err_t sd_card_init(void);

// True once a card is mounted. Everything that touches /sdcard checks this first.
bool sd_card_available(void);

// Which of the three outcomes sd_card_init() reached. Valid before sd_card_init() runs
// too -- it reads as SD_CARD_ABSENT, which is the safe assumption.
sd_card_state_t sd_card_get_state(void);

// The error sd_card_init() ended on, for reporting a cause rather than just a state.
// ESP_OK once mounted.
esp_err_t sd_card_last_error(void);

// Raw card size from the CSD, which is known whenever a card answered at all -- so this
// is meaningful in SD_CARD_NO_FILESYSTEM, where the filesystem cannot be asked. Returns
// 0 if no card was detected.
uint64_t sd_card_get_capacity_bytes(void);

#ifdef __cplusplus
}
#endif
