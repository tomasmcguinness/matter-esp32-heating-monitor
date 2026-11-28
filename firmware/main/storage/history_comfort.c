#include "history_comfort.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "history_format.h"
#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"

static const char *TAG = "history_comfort";

// Not named home-* or room-*: the dates listing and the room scan below pick days and rooms out by
// those prefixes.
#define COMFORT_PATH SD_CARD_MOUNT_POINT "/comfort-days"

#define COMFORT_MAGIC   0x31464D43u // 'CMF1' little-endian
#define COMFORT_VERSION 1

#define SCAN_CHUNK_RECORDS 128

// Today's running figures are refreshed every this many backfill calls -- one a minute -- so the
// Rooms page is at most this many minutes behind without a day's worth of reads on every request.
#define TODAY_REFRESH_CALLS 10

#define MAX_ROOMS 32

#define DATE_LEN 10

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  reserved[3];
} comfort_header_t;

HIST_STATIC_ASSERT(sizeof(comfort_split_t) == 24, "comfort_split_t is part of the cache record");
HIST_STATIC_ASSERT(sizeof(comfort_record_t) == 64, "comfort_record_t must stay 64 bytes");

// rec_home_t and rec_room_t field positions.
#define HOME_DHW_RUNNING     8
#define HOME_HEATING_RUNNING 11
#define ROOM_TEMP            0
#define ROOM_TARGET          (offsetof(rec_room_t, target_temp_c100) / 2)

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

void comfort_add(comfort_split_t *split, bool has_temp, int16_t temp_c100, bool has_target, int16_t target_c100,
                 uint16_t interval_s)
{
    if (!has_temp || !has_target) {
        return;
    }

    int32_t offset = (int32_t)temp_c100 - target_c100;

    if (offset < -COMFORT_BAND_C100) {
        split->under_s += interval_s;
    } else if (offset > COMFORT_BAND_C100) {
        split->over_s += interval_s;
    } else {
        split->on_s += interval_s;
    }

    split->offset_cs += (int64_t)offset * interval_s;
}

static bool field(uint8_t kind, const uint8_t *rec, size_t index, int16_t *out)
{
    uint16_t raw;
    memcpy(&raw, rec + index * 2, 2);

    if (history_field_is_null(kind, index, raw)) {
        return false;
    }

    *out = (int16_t)raw;
    return true;
}

// One bit per home slot: whether the heat pump was space heating. NULL when the home day cannot be
// read; *base and *interval give the slot arithmetic.
static uint8_t *load_heating_bits(const char *date, uint32_t *base, uint16_t *interval, uint32_t *slots)
{
    char path[HISTORY_PATH_MAX];
    if (!history_path_for_date(KIND_HOME, 0, date, path, sizeof(path))) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    history_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || history_check_header(&hdr, KIND_HOME) != HISTORY_HEADER_OK) {
        fclose(f);
        return NULL;
    }

    // Sized for the longest local day at this interval: 25 hours at a DST change.
    uint32_t max_slots = (25u * 3600u) / hdr.interval_s + 1;
    uint8_t *bits      = heap_caps_calloc((max_slots + 7) / 8, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buf       = heap_caps_malloc(SCAN_CHUNK_RECORDS * sizeof(rec_home_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!bits || !buf) {
        free(bits);
        free(buf);
        fclose(f);
        return NULL;
    }

    uint32_t slot = 0;
    size_t   got;

    while (slot < max_slots && (got = fread(buf, sizeof(rec_home_t), SCAN_CHUNK_RECORDS, f)) > 0) {
        for (size_t i = 0; i < got && slot < max_slots; i++, slot++) {
            int16_t heating = 0;
            int16_t dhw     = 0;
            field(KIND_HOME, buf + i * sizeof(rec_home_t), HOME_HEATING_RUNNING, &heating);
            field(KIND_HOME, buf + i * sizeof(rec_home_t), HOME_DHW_RUNNING, &dhw);

            if (heating == 1 && dhw != 1) {
                bits[slot / 8] |= (uint8_t)(1u << (slot % 8));
            }
        }
    }

    free(buf);
    fclose(f);

    *base     = hdr.base_ts;
    *interval = hdr.interval_s;
    *slots    = slot;
    return bits;
}

// The room's figures for a day, with the home day's heating bits already loaded (or NULL).
static bool scan_room(uint8_t room_id, const char *date, const uint8_t *bits, uint32_t home_base,
                      uint16_t home_interval, uint32_t home_slots, comfort_record_t *out)
{
    memset(out, 0, sizeof(*out));
    strlcpy(out->date, date, sizeof(out->date));
    out->room_id = room_id;
    if (!bits) {
        out->flags |= COMFORT_HEATING_UNKNOWN;
    }

    char path[HISTORY_PATH_MAX];
    if (!history_path_for_date(KIND_ROOM, room_id, date, path, sizeof(path))) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    history_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || history_check_header(&hdr, KIND_ROOM) != HISTORY_HEADER_OK) {
        fclose(f);
        return false;
    }

    uint8_t *buf = heap_caps_malloc(SCAN_CHUNK_RECORDS * sizeof(rec_room_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        return false;
    }

    int16_t current_target = 0;
    bool    has_current    = history_logger_room_target(room_id, &current_target);

    uint32_t slot = 0;
    size_t   got;

    while ((got = fread(buf, sizeof(rec_room_t), SCAN_CHUNK_RECORDS, f)) > 0) {
        for (size_t i = 0; i < got; i++, slot++) {
            const uint8_t *rec = buf + i * sizeof(rec_room_t);

            int16_t temp   = 0;
            int16_t target = 0;
            bool    has_temp   = field(KIND_ROOM, rec, ROOM_TEMP, &temp);
            bool    has_target = field(KIND_ROOM, rec, ROOM_TARGET, &target);

            if (has_temp && !has_target && has_current) {
                target     = current_target;
                has_target = true;
                out->flags |= COMFORT_TARGET_ESTIMATED;
            }

            comfort_add(&out->all, has_temp, temp, has_target, target, hdr.interval_s);

            if (bits) {
                // Joined on time rather than index, in case the two files were written at
                // different intervals.
                uint32_t ts = hdr.base_ts + slot * hdr.interval_s;
                if (ts >= home_base) {
                    uint32_t hs = (ts - home_base) / home_interval;
                    if (hs < home_slots && (bits[hs / 8] >> (hs % 8)) & 1u) {
                        comfort_add(&out->heating, has_temp, temp, has_target, target, hdr.interval_s);
                    }
                }
            }
        }
    }

    free(buf);
    fclose(f);
    return true;
}

bool history_comfort_scan_day(uint8_t room_id, const char *date, comfort_record_t *out)
{
    uint32_t base = 0, slots = 0;
    uint16_t interval = 1;
    uint8_t *bits = load_heating_bits(date, &base, &interval, &slots);
    bool     ok   = scan_room(room_id, date, bits, base, interval, slots, out);
    free(bits);
    return ok;
}

// The ids of every room with a history directory, in no particular order.
static size_t list_rooms(uint8_t *ids, size_t max)
{
    DIR *dir = opendir(SD_CARD_MOUNT_POINT);
    if (!dir) {
        return 0;
    }

    size_t         n = 0;
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL && n < max) {
        unsigned id;
        char     tail;
        if (sscanf(ent->d_name, "room-%u%c", &id, &tail) == 1 && id < COMFORT_DAY_DONE) {
            ids[n++] = (uint8_t)id;
        }
    }

    closedir(dir);
    return n;
}

// Every room's figures for a day, through `emit`. Rooms with no file for the day are skipped.
static void scan_all_rooms(const char *date, comfort_emit_fn emit, void *ctx)
{
    uint8_t ids[MAX_ROOMS];
    size_t  n = list_rooms(ids, MAX_ROOMS);

    uint32_t base = 0, slots = 0;
    uint16_t interval = 1;
    uint8_t *bits = n ? load_heating_bits(date, &base, &interval, &slots) : NULL;

    for (size_t i = 0; i < n; i++) {
        comfort_record_t rec;
        if (scan_room(ids[i], date, bits, base, interval, slots, &rec)) {
            emit(&rec, ctx);
        }
    }

    free(bits);
}

// ---------------------------------------------------------------------------
// The cache
// ---------------------------------------------------------------------------

// Only touched from the flush task, except s_today, which the httpd task reads under s_today_lock.
static char s_last_date[12];
static char s_caught_up_on[12];
static int  s_calls;

static comfort_record_t *s_today;
static size_t            s_today_count;
static char              s_today_date[12];
static portMUX_TYPE      s_today_lock = portMUX_INITIALIZER_UNLOCKED;

static FILE *open_cache_for_read(void)
{
    FILE *f = fopen(COMFORT_PATH, "rb");
    if (!f) {
        return NULL;
    }

    comfort_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != COMFORT_MAGIC || hdr.version != COMFORT_VERSION) {
        fclose(f);
        return NULL;
    }
    return f;
}

static bool load_cursor(void)
{
    s_last_date[0] = '\0';

    FILE *f = open_cache_for_read();
    if (f) {
        // Only the last record matters, and the file is append-only, so seek to it.
        comfort_record_t rec;
        if (fseek(f, -(long)sizeof(rec), SEEK_END) == 0 && ftell(f) >= (long)sizeof(comfort_header_t) &&
            fread(&rec, sizeof(rec), 1, f) == 1) {
            rec.date[sizeof(rec.date) - 1] = '\0';
            strlcpy(s_last_date, rec.date, sizeof(s_last_date));
        }
        fclose(f);
        return true;
    }

    f = fopen(COMFORT_PATH, "wb");
    if (!f) {
        ESP_LOGW(TAG, "Cannot create %s", COMFORT_PATH);
        return false;
    }

    comfort_header_t hdr = {.magic = COMFORT_MAGIC, .version = COMFORT_VERSION};
    bool             ok  = fwrite(&hdr, sizeof(hdr), 1, f) == 1;
    fclose(f);
    return ok;
}

static bool is_date(const char *s)
{
    return strlen(s) == DATE_LEN && s[4] == '-' && s[7] == '-';
}

static void append_record(const comfort_record_t *rec, void *ctx)
{
    FILE *f = (FILE *)ctx;
    fwrite(rec, sizeof(*rec), 1, f);
}

typedef struct {
    comfort_record_t *recs;
    size_t            count;
} today_collect_t;

static void collect_today(const comfort_record_t *rec, void *ctx)
{
    today_collect_t *c = (today_collect_t *)ctx;
    if (c->count < MAX_ROOMS) {
        c->recs[c->count++] = *rec;
    }
}

static void refresh_today(const char *today)
{
    comfort_record_t *recs = heap_caps_calloc(MAX_ROOMS, sizeof(comfort_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recs) {
        return;
    }

    today_collect_t c = {.recs = recs, .count = 0};
    scan_all_rooms(today, collect_today, &c);

    taskENTER_CRITICAL(&s_today_lock);
    comfort_record_t *old = s_today;
    s_today               = recs;
    s_today_count         = c.count;
    strlcpy(s_today_date, today, sizeof(s_today_date));
    taskEXIT_CRITICAL(&s_today_lock);

    free(old);
}

void history_comfort_backfill_one(void)
{
    if (!sd_card_available() || !clock_is_valid()) {
        return;
    }

    char today[12];
    local_date_string((uint32_t)time(NULL), today, sizeof(today));

    // Today first: it is what the Rooms page shows most of, and it is cheap next to a backfill.
    if (s_calls++ % TODAY_REFRESH_CALLS == 0 || strcmp(s_today_date, today) != 0) {
        refresh_today(today);
    }

    if (strcmp(s_caught_up_on, today) == 0) {
        return;
    }

    if (!load_cursor()) {
        return;
    }

    // The oldest home day newer than the cursor and older than today. Home days are the day list,
    // as for cop-totals: the logger writes a home file every day it writes any room file.
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

    FILE *f = fopen(COMFORT_PATH, "ab");
    if (!f) {
        ESP_LOGW(TAG, "Cannot append to %s", COMFORT_PATH);
        return;
    }

    scan_all_rooms(next, append_record, f);

    // Always last, so the cursor -- the last record's date -- reaches this day only once every
    // room of it is on the card.
    comfort_record_t done;
    memset(&done, 0, sizeof(done));
    strlcpy(done.date, next, sizeof(done.date));
    done.room_id = COMFORT_DAY_DONE;
    bool ok      = fwrite(&done, sizeof(done), 1, f) == 1;
    fclose(f);

    if (ok) {
        ESP_LOGI(TAG, "Summarised %s", next);
    }
}

bool history_comfort_each(const char *from_date, comfort_emit_fn emit, void *ctx)
{
    bool  ok = false;
    FILE *f  = open_cache_for_read();

    if (f) {
        ok = true;
        comfort_record_t rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            rec.date[sizeof(rec.date) - 1] = '\0';
            if (rec.room_id != COMFORT_DAY_DONE && strcmp(rec.date, from_date) >= 0) {
                emit(&rec, ctx);
            }
        }
        fclose(f);
    }

    // Copied out under the lock, then emitted without it: emit writes to a socket. In PSRAM, since
    // 2 KB is a lot of an httpd task's stack.
    comfort_record_t *today = heap_caps_malloc(MAX_ROOMS * sizeof(comfort_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t            n     = 0;

    if (today) {
        taskENTER_CRITICAL(&s_today_lock);
        n = s_today ? s_today_count : 0;
        if (n) {
            memcpy(today, s_today, n * sizeof(comfort_record_t));
        }
        taskEXIT_CRITICAL(&s_today_lock);
    }

    for (size_t i = 0; i < n; i++) {
        emit(&today[i], ctx);
    }

    free(today);
    return ok;
}
