#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#pragma once

// Length of the hex-encoded pairing token, excluding the NUL terminator.
#define PAIRING_TOKEN_HEX_LEN 32

// Length of the hex-encoded device id, excluding the NUL terminator.
#define PAIRING_DEVICE_ID_HEX_LEN 8

typedef struct
{
    char token[PAIRING_TOKEN_HEX_LEN + 1];
    char device_id[PAIRING_DEVICE_ID_HEX_LEN + 1];
} pairing_manager_t;

// Loads the pairing token from NVS, generating and persisting a new one on first boot.
// The device id is derived from the base MAC and is not persisted.
void pairing_manager_init(pairing_manager_t *manager);

// Discards the current token and issues a new one. Any companion app paired against the
// old token has to re-scan the QR code.
esp_err_t pairing_regenerate_token(pairing_manager_t *manager);

const char *pairing_get_token(pairing_manager_t *manager);
const char *pairing_get_device_id(pairing_manager_t *manager);

// True when `presented` matches the stored token. A NULL or empty `presented` is never a
// match. Comparison is length-constant to keep the token off a timing side channel.
bool pairing_token_matches(pairing_manager_t *manager, const char *presented);

esp_err_t load_pairing_from_nvs(pairing_manager_t *manager);
esp_err_t save_pairing_to_nvs(pairing_manager_t *manager);
