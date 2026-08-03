#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "pairing_manager.h"

#define NVS_NAMESPACE "pairing"
#define NVS_KEY "token"

// 16 random bytes, hex-encoded, is PAIRING_TOKEN_HEX_LEN characters.
#define PAIRING_TOKEN_BYTES (PAIRING_TOKEN_HEX_LEN / 2)

static const char *TAG = "pairing_manager";

static void bytes_to_hex(const uint8_t *bytes, size_t length, char *out)
{
    static const char hex_digits[] = "0123456789abcdef";

    for (size_t i = 0; i < length; i++)
    {
        out[i * 2] = hex_digits[bytes[i] >> 4];
        out[i * 2 + 1] = hex_digits[bytes[i] & 0x0f];
    }

    out[length * 2] = '\0';
}

static void generate_token(pairing_manager_t *manager)
{
    uint8_t random_bytes[PAIRING_TOKEN_BYTES];

    esp_fill_random(random_bytes, sizeof(random_bytes));

    bytes_to_hex(random_bytes, sizeof(random_bytes), manager->token);
}

// The device id is a stable, human-quotable handle for this board. It is derived from the
// base MAC rather than stored, so it survives an NVS erase.
static void derive_device_id(pairing_manager_t *manager)
{
    uint8_t mac[6];

    if (esp_efuse_mac_get_default(mac) != ESP_OK)
    {
        ESP_LOGW(TAG, "Could not read the base MAC; falling back to a placeholder device id");
        strncpy(manager->device_id, "00000000", sizeof(manager->device_id));
        return;
    }

    // The last four octets are the device-specific part of the MAC, and hex-encode to
    // exactly PAIRING_DEVICE_ID_HEX_LEN characters.
    bytes_to_hex(&mac[2], 4, manager->device_id);
}

void pairing_manager_init(pairing_manager_t *manager)
{
    if (manager == NULL)
    {
        ESP_LOGE(TAG, "Manager pointer is NULL!");
        return;
    }

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    memset(manager, 0, sizeof(pairing_manager_t));

    derive_device_id(manager);

    esp_err_t load_err = load_pairing_from_nvs(manager);

    if (load_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded pairing token from NVS (device id %s)", manager->device_id);
        return;
    }

    ESP_LOGW(TAG, "No pairing token in NVS (err: 0x%x); issuing a new one", load_err);

    generate_token(manager);

    esp_err_t save_err = save_pairing_to_nvs(manager);

    if (save_err != ESP_OK)
    {
        // Not fatal: the token still works until the next reboot, at which point a new one
        // is issued and the companion app has to re-pair.
        ESP_LOGE(TAG, "Failed to persist the pairing token: 0x%x", save_err);
    }

    ESP_LOGI(TAG, "Issued a new pairing token (device id %s)", manager->device_id);
}

esp_err_t pairing_regenerate_token(pairing_manager_t *manager)
{
    if (manager == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    generate_token(manager);

    return save_pairing_to_nvs(manager);
}

const char *pairing_get_token(pairing_manager_t *manager)
{
    return manager ? manager->token : "";
}

const char *pairing_get_device_id(pairing_manager_t *manager)
{
    return manager ? manager->device_id : "";
}

bool pairing_token_matches(pairing_manager_t *manager, const char *presented)
{
    if (manager == NULL || presented == NULL || manager->token[0] == '\0')
    {
        return false;
    }

    if (strlen(presented) != PAIRING_TOKEN_HEX_LEN)
    {
        return false;
    }

    uint8_t difference = 0;

    for (size_t i = 0; i < PAIRING_TOKEN_HEX_LEN; i++)
    {
        difference |= (uint8_t)(manager->token[i] ^ presented[i]);
    }

    return difference == 0;
}

esp_err_t load_pairing_from_nvs(pairing_manager_t *manager)
{
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK)
    {
        return err;
    }

    size_t length = sizeof(manager->token);

    err = nvs_get_str(nvs_handle, NVS_KEY, manager->token, &length);

    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        return err;
    }

    if (strlen(manager->token) != PAIRING_TOKEN_HEX_LEN)
    {
        ESP_LOGW(TAG, "Stored pairing token is malformed; it will be replaced");
        manager->token[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t save_pairing_to_nvs(pairing_manager_t *manager)
{
    if (!manager)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY, manager->token);

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    return err;
}
