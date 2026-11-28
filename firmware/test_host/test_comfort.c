// Host tests for history_comfort.c: time at target per room (the Rooms page's "Last 7 days",
// "Avg vs target" and grid, and the daily chart on a room's page).
//
// Built by run.sh; no board needed. Day files are written to a temporary directory, which the
// history_path_for_date() stub below points at.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "history_comfort.h"
#include "history_format.h"
#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                \
    do {                                                           \
        s_checks++;                                                \
        if (!(cond)) {                                             \
            s_failures++;                                          \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static char s_dir[64];
static int16_t s_current_target = 2100;
static bool    s_room_exists    = true;

// --- Stubs -----------------------------------------------------------------------------------------

void local_date_string(uint32_t ts, char *buf, size_t len)
{
    (void)ts;
    strlcpy(buf, "2026-01-02", len);
}

bool history_path_for_date(uint8_t kind, uint8_t inst, const char *date, char *out, size_t out_len)
{
    if (kind == KIND_HOME) {
        snprintf(out, out_len, "%s/home-%s", s_dir, date);
    } else {
        snprintf(out, out_len, "%s/room-%u-%s", s_dir, (unsigned)inst, date);
    }
    return true;
}

bool sd_card_available(void)
{
    return false;
}

bool clock_is_valid(void)
{
    return false;
}

bool history_logger_room_target(uint8_t room_id, int16_t *target_c100)
{
    (void)room_id;
    *target_c100 = s_current_target;
    return s_room_exists;
}

// --- Files -----------------------------------------------------------------------------------------

#define INTERVAL 5
#define BASE     1767225600u // 2026-01-01T00:00:00Z

static FILE *start(uint8_t kind, uint8_t inst, uint16_t record_size)
{
    char path[HISTORY_PATH_MAX];
    history_path_for_date(kind, inst, "2026-01-01", path, sizeof(path));
    FILE *f = fopen(path, "wb");

    history_header_t hdr = {
        .magic       = HISTORY_MAGIC,
        .version     = history_version(kind),
        .sensor_kind = kind,
        .interval_s  = INTERVAL,
        .base_ts     = BASE,
        .record_size = record_size,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);
    return f;
}

static void room_slots(FILE *f, int16_t temp, int16_t target, int times)
{
    rec_room_t r;
    history_fill_sentinel(KIND_ROOM, &r);
    r.current_temp_c100 = temp;
    r.target_temp_c100  = target;
    for (int i = 0; i < times; i++) {
        fwrite(&r, sizeof(r), 1, f);
    }
}

static void home_slots(FILE *f, int16_t heating, int16_t dhw, int times)
{
    rec_home_t r;
    history_fill_sentinel(KIND_HOME, &r);
    r.heating_running = heating;
    r.dhw_running     = dhw;
    for (int i = 0; i < times; i++) {
        fwrite(&r, sizeof(r), 1, f);
    }
}

static void remove_files(void)
{
    char path[HISTORY_PATH_MAX];
    history_path_for_date(KIND_HOME, 0, "2026-01-01", path, sizeof(path));
    unlink(path);
    history_path_for_date(KIND_ROOM, 3, "2026-01-01", path, sizeof(path));
    unlink(path);
}

// --- Tests -----------------------------------------------------------------------------------------

// The band is inclusive on both sides, and the mean offset is time-weighted.
static void test_band(void)
{
    comfort_split_t s = {0};

    comfort_add(&s, true, 2100 + COMFORT_BAND_C100, true, 2100, 10);     // on, at the upper edge
    comfort_add(&s, true, 2100 - COMFORT_BAND_C100, true, 2100, 10);     // on, at the lower edge
    comfort_add(&s, true, 2100 + COMFORT_BAND_C100 + 1, true, 2100, 10); // over
    comfort_add(&s, true, 1900, true, 2100, 30);                         // under
    comfort_add(&s, false, 2500, true, 2100, 10);                        // no temperature
    comfort_add(&s, true, 2500, false, 2100, 10);                        // no target

    CHECK(s.on_s == 20);
    CHECK(s.over_s == 10);
    CHECK(s.under_s == 30);
    CHECK(s.offset_cs == (int64_t)20 * 10 - 20 * 10 + 21 * 10 - 200 * 30);
}

// A day with the target recorded: judged against it, split by the home's heating flag, not
// estimated. Hot water takes precedence over heating, as on the mode strip.
static void test_recorded_target(void)
{
    FILE *room = start(KIND_ROOM, 3, sizeof(rec_room_t));
    room_slots(room, 2200, 2100, 10);          // over, heating
    room_slots(room, 2100, 2100, 10);          // on, heating with hot water: not heating
    room_slots(room, 1800, 1800, 10);          // on, retargeted to 18, idle
    room_slots(room, HIST_NULL_I16, 2100, 10); // no reading
    fclose(room);

    FILE *home = start(KIND_HOME, 0, sizeof(rec_home_t));
    home_slots(home, 1, 0, 10);
    home_slots(home, 1, 1, 10);
    home_slots(home, 0, 0, 20);
    fclose(home);

    comfort_record_t rec;
    CHECK(history_comfort_scan_day(3, "2026-01-01", &rec));
    CHECK(rec.room_id == 3);
    CHECK(strcmp(rec.date, "2026-01-01") == 0);
    CHECK(rec.flags == 0);
    CHECK(rec.all.over_s == 10 * INTERVAL);
    CHECK(rec.all.on_s == 20 * INTERVAL);
    CHECK(rec.all.under_s == 0);
    CHECK(rec.heating.over_s == 10 * INTERVAL);
    CHECK(rec.heating.on_s == 0);
    CHECK(rec.heating.offset_cs == 100LL * 10 * INTERVAL);

    remove_files();
}

// A day from before the target was recorded uses the room's target now, and says so. With no home
// file, the heating split is empty and flagged unknown.
static void test_estimated_target_and_no_home(void)
{
    FILE *room = start(KIND_ROOM, 3, sizeof(rec_room_t));
    room_slots(room, 2000, HIST_NULL_I16, 12);
    fclose(room);

    s_current_target = 2100;

    comfort_record_t rec;
    CHECK(history_comfort_scan_day(3, "2026-01-01", &rec));
    CHECK(rec.flags == (COMFORT_TARGET_ESTIMATED | COMFORT_HEATING_UNKNOWN));
    CHECK(rec.all.under_s == 12 * INTERVAL);
    CHECK(rec.all.offset_cs == -100LL * 12 * INTERVAL);
    CHECK(rec.heating.under_s == 0 && rec.heating.on_s == 0 && rec.heating.over_s == 0);

    // A room that has since been deleted has no target to estimate with, so nothing counts.
    s_room_exists = false;
    CHECK(history_comfort_scan_day(3, "2026-01-01", &rec));
    CHECK(rec.all.under_s == 0 && rec.all.on_s == 0 && rec.all.over_s == 0);
    s_room_exists = true;

    remove_files();
}

// No file for the room that day.
static void test_missing_room(void)
{
    comfort_record_t rec;
    CHECK(!history_comfort_scan_day(3, "2026-01-01", &rec));
    CHECK(rec.room_id == 3);
}

int main(void)
{
    strlcpy(s_dir, "/tmp/test_comfort_XXXXXX", sizeof(s_dir));
    if (!mkdtemp(s_dir)) {
        perror("mkdtemp");
        return 1;
    }

    test_band();
    test_recorded_target();
    test_estimated_target_and_no_home();
    test_missing_room();

    rmdir(s_dir);

    printf("test_comfort: %d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
