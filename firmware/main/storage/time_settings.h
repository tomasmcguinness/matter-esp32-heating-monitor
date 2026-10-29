#pragma once

#include "esp_err.h"

#include "timezones.h"

// The device's time zone and NTP server, kept in the NVS namespace "time" and edited from the
// Settings page. Both are read once, by time_sync.c, so a change takes effect on restart.

#ifdef __cplusplus
extern "C" {
#endif

#define TIME_NTP_SERVER_MAX     64
#define TIME_NTP_SERVER_DEFAULT "pool.ntp.org"

typedef struct
{
    char zone[TIMEZONE_NAME_MAX];        // an IANA name from timezones.h
    char ntp_server[TIME_NTP_SERVER_MAX];
} time_settings_t;

// The saved settings, or Europe/London and pool.ntp.org for anything not saved. A saved zone
// that is no longer in the table also reads back as the default.
void time_settings_load(time_settings_t *settings);

// ESP_ERR_INVALID_ARG unless the zone is in the table and the NTP server is a non-empty host name
// with no whitespace.
esp_err_t time_settings_save(const time_settings_t *settings);

#ifdef __cplusplus
}
#endif
