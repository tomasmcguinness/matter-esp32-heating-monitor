#include "history_totals.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "history_format.h"
#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"

static const char *TAG = "history_totals";

#define TOTALS_PATH SD_CARD_MOUNT_POINT "/cop-totals"

#define HISTORY_TOTALS_MAGIC   0x31544F43u // 'COT1' little-endian
#define HISTORY_TOTALS_VERSION 1

// Records read per disk pass while scanning a day. Allocated per scan rather than static,
// because a scan runs on the flush task and on the httpd task and the two can overlap.
#define SCAN_CHUNK_RECORDS 64

#define DATE_LEN 10 // YYYY-MM-DD

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  reserved[3];
} totals_header_t;

// Keyed on the date string rather than base_ts, because it is compared against file names. The
// date is padded to 16 bytes so the totals sit 8-byte aligned without packing the struct.
typedef struct {
    char             date[16]; // YYYY-MM-DD, NUL-terminated
    history_totals_t totals;
} totals_record_t;

HIST_STATIC_ASSERT(sizeof(totals_record_t) == 64, "totals_record_t must stay 64 bytes");

// rec_home_t field positions.
#define FIELD_HEAT_POWER      0
#define FIELD_ELEC_POWER      1
#define FIELD_DHW_RUNNING     8
#define FIELD_HEATING_RUNNING 11

// A signed home field, or false when it holds the sentinel.
static bool home_field(const uint8_t *rec, size_t index, int16_t *out)
{
    uint16_t raw;
    memcpy(&raw, rec + index * 2, 2);

    if (history_field_is_null(KIND_HOME, index, raw)) {
        return false;
    }

    *out = (int16_t)raw;
    return true;
}

void history_totals_add(history_totals_t *totals, const uint8_t *rec, uint16_t interval_s)
{
    int16_t heat    = 0;
    int16_t elec    = 0;
    int16_t dhw     = 0;
    int16_t heating = 0;

    bool has_heat = home_field(rec, FIELD_HEAT_POWER, &heat);
    bool has_elec = home_field(rec, FIELD_ELEC_POWER, &elec);

    // A missing mode is "not in that mode", which leaves the slot in `overall` only.
    home_field(rec, FIELD_DHW_RUNNING, &dhw);
    home_field(rec, FIELD_HEATING_RUNNING, &heating);

    energy_pair_t *mode = dhw == 1 ? &totals->hot_water : heating == 1 ? &totals->heating : NULL;

    if (has_heat) {
        int64_t ws = (int64_t)heat * interval_s;
        totals->overall.heat_ws += ws;
        if (mode) {
            mode->heat_ws += ws;
        }
    }

    if (has_elec) {
        int64_t ws = (int64_t)elec * interval_s;
        totals->overall.elec_ws += ws;
        if (mode) {
            mode->elec_ws += ws;
        }
    }
}

void history_totals_accumulate(history_totals_t *into, const history_totals_t *from)
{
    into->overall.heat_ws   += from->overall.heat_ws;
    into->overall.elec_ws   += from->overall.elec_ws;
    into->heating.heat_ws   += from->heating.heat_ws;
    into->heating.elec_ws   += from->heating.elec_ws;
    into->hot_water.heat_ws += from->hot_water.heat_ws;
    into->hot_water.elec_ws += from->hot_water.elec_ws;
}

bool history_totals_scan_file(const char *path, history_totals_t *out)
{
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    history_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != HISTORY_MAGIC ||
        hdr.version != HISTORY_VERSION || hdr.sensor_kind != KIND_HOME ||
        hdr.record_size != sizeof(rec_home_t)) {
        fclose(f);
        ESP_LOGW(TAG, "Not a readable home series: %s", path);
        return false;
    }

    uint8_t *buf = malloc(SCAN_CHUNK_RECORDS * sizeof(rec_home_t));
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t got;
    while ((got = fread(buf, sizeof(rec_home_t), SCAN_CHUNK_RECORDS, f)) > 0) {
        for (size_t i = 0; i < got; i++) {
            history_totals_add(out, buf + i * sizeof(rec_home_t), hdr.interval_s);
        }
    }

    free(buf);
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// The per-day cache
// ---------------------------------------------------------------------------

// The date of the newest cached day, and the day on which the card was last found to hold
// nothing newer to summarise. Both are only touched from the flush task.
static char s_last_date[12];
static char s_caught_up_on[12];

static bool is_date(const char *s)
{
    return strlen(s) == DATE_LEN && s[4] == '-' && s[7] == '-';
}

// Opens the cache for reading, positioned after a header this firmware wrote. NULL otherwise.
static FILE *open_cache_for_read(void)
{
    FILE *f = fopen(TOTALS_PATH, "rb");
    if (!f) {
        return NULL;
    }

    totals_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != HISTORY_TOTALS_MAGIC ||
        hdr.version != HISTORY_TOTALS_VERSION) {
        fclose(f);
        return NULL;
    }
    return f;
}

// Finds where the cache has got to, starting it afresh if it is missing or from another version.
static bool load_cursor(void)
{
    s_last_date[0] = '\0';

    FILE *f = open_cache_for_read();
    if (f) {
        totals_record_t rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            rec.date[sizeof(rec.date) - 1] = '\0';
            strlcpy(s_last_date, rec.date, sizeof(s_last_date));
        }
        fclose(f);
        return true;
    }

    f = fopen(TOTALS_PATH, "wb");
    if (!f) {
        ESP_LOGW(TAG, "Cannot create %s", TOTALS_PATH);
        return false;
    }

    totals_header_t hdr = {.magic = HISTORY_TOTALS_MAGIC, .version = HISTORY_TOTALS_VERSION};
    bool            ok  = fwrite(&hdr, sizeof(hdr), 1, f) == 1;
    fclose(f);
    return ok;
}

void history_totals_backfill_one(void)
{
    if (!sd_card_available() || !clock_is_valid()) {
        return;
    }

    char today[12];
    local_date_string((uint32_t)time(NULL), today, sizeof(today));

    // The directory holds a file for every day ever recorded, so it is walked once a day rather
    // than once a minute: after it comes up empty, nothing new can complete until midnight.
    if (strcmp(s_caught_up_on, today) == 0) {
        return;
    }

    // Read from the file every time rather than remembered, so a cache deleted or replaced while
    // the device is running is started again instead of appended to without a header. It is a
    // few kilobytes, and this is reached once a minute at most.
    if (!load_cursor()) {
        return;
    }

    // The oldest home day newer than the cursor and older than today. ISO dates sort as strings.
    DIR *dir = opendir(SD_CARD_MOUNT_POINT);
    if (!dir) {
        return;
    }

    char           next[12] = "";
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        char date[16];
        if (sscanf(ent->d_name, "home-%15s", date) != 1 || !is_date(date)) {
            continue;
        }
        if (strcmp(date, s_last_date) <= 0 || strcmp(date, today) >= 0) {
            continue;
        }
        if (next[0] == '\0' || strcmp(date, next) < 0) {
            strlcpy(next, date, sizeof(next));
        }
    }
    closedir(dir);

    if (next[0] == '\0') {
        strlcpy(s_caught_up_on, today, sizeof(s_caught_up_on));
        return;
    }

    totals_record_t rec;
    memset(&rec, 0, sizeof(rec));
    strlcpy(rec.date, next, sizeof(rec.date));

    // A day that cannot be read is still recorded, as zeros, so the cursor moves past it rather
    // than retrying the same file every minute and never reaching the days after it.
    char path[HISTORY_PATH_MAX];
    if (history_path_for_date(KIND_HOME, 0, next, path, sizeof(path))) {
        history_totals_scan_file(path, &rec.totals);
    }

    FILE *f = fopen(TOTALS_PATH, "ab");
    if (!f) {
        ESP_LOGW(TAG, "Cannot append to %s", TOTALS_PATH);
        return;
    }

    bool ok = fwrite(&rec, sizeof(rec), 1, f) == 1;
    fclose(f);

    if (ok) {
        ESP_LOGI(TAG, "Summarised %s", next);
    }
}

uint32_t history_totals_sum_cached(history_totals_t *sum, char *first_date, size_t first_date_len)
{
    uint32_t days = 0;

    memset(sum, 0, sizeof(*sum));
    if (first_date_len) {
        first_date[0] = '\0';
    }

    FILE *f = open_cache_for_read();
    if (!f) {
        return 0;
    }

    // A record the flush task is part-way through appending reads short and ends the loop.
    totals_record_t rec;
    while (fread(&rec, sizeof(rec), 1, f) == 1) {
        rec.date[sizeof(rec.date) - 1] = '\0';

        if (days == 0) {
            strlcpy(first_date, rec.date, first_date_len);
        }
        history_totals_accumulate(sum, &rec.totals);
        days++;
    }

    fclose(f);
    return days;
}
