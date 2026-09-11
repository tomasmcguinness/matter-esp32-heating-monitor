#include "time_sync.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "time_sync";

// UK. Matches matter-esp32-home-energy-manager (main.cpp:104).
#define LOCAL_TZ "GMT0BST,M3.5.0/1,M10.5.0"

static volatile bool s_clock_valid = false;
static bool          s_started     = false;

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

    setenv("TZ", LOCAL_TZ, 1);
    tzset();

    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started");
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
