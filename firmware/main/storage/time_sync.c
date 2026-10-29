#include "time_sync.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

#include "time_settings.h"
#include "timezones.h"

static const char *TAG = "time_sync";

static volatile bool s_clock_valid = false;
static bool          s_started     = false;

// Read from NVS once, at the first call that needs them, and kept for the life of the boot: the
// logger and everything else that works in local days must not see the zone change under them.
// SNTP keeps the pointer to the server name, so it has to live here rather than on a stack.
static bool            s_loaded = false;
static time_settings_t s_settings;

static void load_settings(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    time_settings_load(&s_settings);

    // time_settings_load() only hands back a zone that is in the table.
    setenv("TZ", timezones_posix_for(s_settings.zone), 1);
    tzset();

    ESP_LOGI(TAG, "Time zone %s, NTP server %s", s_settings.zone, s_settings.ntp_server);
}

static void on_time_sync(struct timeval *tv)
{
    if (!s_clock_valid) {
        ESP_LOGI(TAG, "Clock set from SNTP: %lld", (long long)tv->tv_sec);
    }
    s_clock_valid = true;
}

void time_sync_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;

    load_settings();

    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_settings.ntp_server);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started");
}

const char *time_sync_zone_name(void)
{
    load_settings();
    return s_settings.zone;
}

bool clock_is_valid(void)
{
    return s_clock_valid;
}

uint32_t local_midnight_for(uint32_t ts)
{
    time_t    t = (time_t)ts;
    struct tm tm_info;

    localtime_r(&t, &tm_info);
    tm_info.tm_hour  = 0;
    tm_info.tm_min   = 0;
    tm_info.tm_sec   = 0;
    tm_info.tm_isdst = -1; // let mktime work out DST for that date

    return (uint32_t)mktime(&tm_info);
}

void local_date_string(uint32_t ts, char *buf, size_t len)
{
    time_t    t = (time_t)ts;
    struct tm tm_info;

    localtime_r(&t, &tm_info);
    strftime(buf, len, "%Y-%m-%d", &tm_info);
}
