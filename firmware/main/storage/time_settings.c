#include "time_settings.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "nvs.h"

#define NVS_NAMESPACE "time"

void time_settings_load(time_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    strlcpy(settings->zone, TIMEZONE_DEFAULT, sizeof(settings->zone));
    strlcpy(settings->ntp_server, TIME_NTP_SERVER_DEFAULT, sizeof(settings->ntp_server));

    nvs_handle_t nvs;

    // Nothing saved yet is the normal first-boot case, and the defaults stand.
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    char   zone[TIMEZONE_NAME_MAX];
    size_t len = sizeof(zone);
    if (nvs_get_str(nvs, "zone", zone, &len) == ESP_OK && timezones_posix_for(zone) != NULL) {
        strlcpy(settings->zone, zone, sizeof(settings->zone));
    }

    char ntp[TIME_NTP_SERVER_MAX];
    len = sizeof(ntp);
    if (nvs_get_str(nvs, "ntp", ntp, &len) == ESP_OK && ntp[0] != '\0') {
        strlcpy(settings->ntp_server, ntp, sizeof(settings->ntp_server));
    }

    nvs_close(nvs);
}

static bool valid_host(const char *host)
{
    if (host[0] == '\0') {
        return false;
    }
    for (const char *p = host; *p; p++) {
        if (isspace((unsigned char)*p) || !isprint((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

esp_err_t time_settings_save(const time_settings_t *settings)
{
    if (timezones_posix_for(settings->zone) == NULL || !valid_host(settings->ntp_server)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "zone", settings->zone);
    if (err == ESP_OK) err = nvs_set_str(nvs, "ntp", settings->ntp_server);
    if (err == ESP_OK) err = nvs_commit(nvs);

    nvs_close(nvs);

    return err;
}
