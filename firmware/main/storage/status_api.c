#include "status_api.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sd_card.h"

static const char *TAG = "status_api";

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:    return "Power on";
    case ESP_RST_EXT:        return "External pin";
    case ESP_RST_SW:         return "Software restart";
    case ESP_RST_PANIC:      return "Panic";
    case ESP_RST_INT_WDT:    return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:   return "Task watchdog";
    case ESP_RST_WDT:        return "Watchdog";
    case ESP_RST_DEEPSLEEP:  return "Deep sleep wake";
    case ESP_RST_BROWNOUT:   return "Brownout";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_EFUSE:      return "eFuse error";
    case ESP_RST_PWR_GLITCH: return "Power glitch";
    case ESP_RST_CPU_LOCKUP: return "CPU lockup";
    default:                 return "Unknown";
    }
}

static void add_sd_section(cJSON *root)
{
    cJSON *sd = cJSON_CreateObject();
    sd_card_state_t state = sd_card_get_state();

    switch (state) {
    case SD_CARD_MOUNTED: {
        cJSON_AddStringToObject(sd, "state", "mounted");
        cJSON_AddNumberToObject(sd, "capacityBytes", (double)sd_card_get_capacity_bytes());

        // f_getfree walks the FAT, so this is the slow part of the response. Acceptable
        // on a page the user loads by hand; it must not move onto a polling path.
        uint64_t total = 0;
        uint64_t avail = 0;

        if (esp_vfs_fat_info(SD_CARD_MOUNT_POINT, &total, &avail) == ESP_OK) {
            cJSON_AddNumberToObject(sd, "totalBytes", (double)total);
            cJSON_AddNumberToObject(sd, "freeBytes", (double)avail);
        } else {
            // Mounted but the free-space query failed. Report the card, not a fake zero.
            cJSON_AddNullToObject(sd, "totalBytes");
            cJSON_AddNullToObject(sd, "freeBytes");
        }
        break;
    }

    case SD_CARD_NO_FILESYSTEM:
        cJSON_AddStringToObject(sd, "state", "no_filesystem");
        cJSON_AddNumberToObject(sd, "capacityBytes", (double)sd_card_get_capacity_bytes());
        cJSON_AddStringToObject(sd, "reason",
                                "The card has no FAT filesystem. Cards over 32 GB are usually "
                                "exFAT, which this firmware cannot read. Reformat it as FAT32.");
        break;

    case SD_CARD_ABSENT:
    default:
        cJSON_AddStringToObject(sd, "state", "absent");
        break;
    }

    if (state != SD_CARD_MOUNTED) {
        cJSON_AddStringToObject(sd, "error", esp_err_to_name(sd_card_last_error()));
    }

    cJSON_AddItemToObject(root, "sd", sd);
}

static void add_device_section(cJSON *root)
{
    cJSON *device = cJSON_CreateObject();
    const esp_app_desc_t *app = esp_app_get_description();

    cJSON_AddStringToObject(device, "firmware", app->version);
    cJSON_AddStringToObject(device, "idf", app->idf_ver);
    cJSON_AddNumberToObject(device, "uptimeSeconds", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(device, "freeHeap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(device, "minFreeHeap", (double)esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(device, "psramFree", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(device, "psramTotal", (double)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddStringToObject(device, "resetReason", reset_reason_name(esp_reset_reason()));

    cJSON_AddItemToObject(root, "device", device);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting device status...");

    cJSON *root = cJSON_CreateObject();

    add_sd_section(root);
    add_device_section(root);

    // A missing or unreadable card is a state to report, not a failed request, so this
    // stays 200 throughout -- the same call the history endpoints make.
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json);

    cJSON_free(json);
    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t status_api_register(httpd_handle_t server)
{
    const httpd_uri_t status_uri = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = status_get_handler,
        .user_ctx = NULL,
    };

    return httpd_register_uri_handler(server, &status_uri);
}
