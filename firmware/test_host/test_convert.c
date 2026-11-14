// Host test for the offline history converter (history_convert/), against day files written to a
// temporary "SD card" copy. Checks that a version 2 day converts with every value where it was and
// defrost unknown, that converted output goes through a second run unchanged, that the input is
// never written to, that a day it cannot convert is reported and left out, and that the defrost
// flag a version 3 day records reads back.
//
// Build and run with ./run.sh.

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "history_convert.h"
#include "history_format.h"

#define IN_DIR   "convert_in"
#define OUT_DIR  "convert_out"
#define OUT2_DIR "convert_out2"

#define V2_DAY  "home-2026-01-01"
#define V3_DAY  "home-2026-01-02"
#define BAD_DAY "home-2026-01-03"

#define SLOTS         1000
#define PARTIAL_BYTES 10
#define DAY_TS        1767225600u // 2026-01-01T00:00:00Z

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

// --- Files ----------------------------------------------------------------------

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        *size = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len > 0 ? (size_t)len : 1);
    *size        = fread(buf, 1, (size_t)len, f);
    fclose(f);
    return buf;
}

static bool same_bytes(const char *a, const char *b)
{
    size_t   na, nb;
    uint8_t *ba = slurp(a, &na);
    uint8_t *bb = slurp(b, &nb);
    bool     same = ba && bb && na == nb && memcmp(ba, bb, na) == 0;
    free(ba);
    free(bb);
    return same;
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// Every entry in a directory, as one string, to check nothing was added or left behind.
static void listing(const char *dir, char *out, size_t len)
{
    out[0] = '\0';
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
            strncat(out, ent->d_name, len - strlen(out) - 1);
            strncat(out, " ", len - strlen(out) - 1);
        }
    }
    closedir(d);
}

static void remove_tree(const char *dir)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) {
        printf("could not remove %s\n", dir);
    }
}

// The value the version 2 day holds in field `f` of slot `i`. Every field is exercised, the
// unsigned ones included, and every so often a field holds its sentinel.
static uint16_t v2_value(size_t i, size_t f)
{
    if ((i + f) % 97 == 0) {
        return ((history_unsigned_mask(KIND_HOME) >> f) & 1u) ? HIST_NULL_U16 : (uint16_t)HIST_NULL_I16;
    }
    if (f == 8 || f == 11) { // dhw_running, heating_running
        return (uint16_t)((i / 50 + f) % 2);
    }
    if (f == 0) { // heat power can go negative, as it does during a defrost
        return (uint16_t)(int16_t)((int)(i % 400) - 100);
    }
    return (uint16_t)(i * 7 + f * 1000);
}

// A day as firmware before the widening wrote it, ending in a partial record as a power cut would
// leave it.
static void write_v2_day(void)
{
    FILE            *f   = fopen(IN_DIR "/" V2_DAY, "wb");
    history_header_t hdr = {.magic = HISTORY_MAGIC, .version = HISTORY_HOME_V2_VERSION, .sensor_kind = KIND_HOME,
                            .interval_s = 5, .base_ts = DAY_TS, .record_size = HISTORY_HOME_V2_RECORD_SIZE};
    fwrite(&hdr, sizeof(hdr), 1, f);

    for (size_t i = 0; i < SLOTS; i++) {
        for (size_t fi = 0; fi < HISTORY_HOME_V2_RECORD_SIZE / 2; fi++) {
            uint16_t v = v2_value(i, fi);
            fwrite(&v, 2, 1, f);
        }
    }

    static const uint8_t partial[PARTIAL_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    fwrite(partial, 1, sizeof(partial), f);
    fclose(f);
}

// What defrosting holds in slot `i` of the version 3 day: two defrosts, one inside hot water, and
// a stretch where the heat pump's subscription was lost and nothing was recorded.
static int16_t v3_defrost(size_t i)
{
    if (i >= 700 && i < 750) {
        return HIST_NULL_I16;
    }
    return (i >= 100 && i < 160) || (i >= 400 && i < 420) ? 1 : 0;
}

// A day the current firmware recorded.
static void write_v3_day(void)
{
    FILE            *f   = fopen(IN_DIR "/" V3_DAY, "wb");
    history_header_t hdr = {.magic = HISTORY_MAGIC, .version = HISTORY_HOME_VERSION, .sensor_kind = KIND_HOME,
                            .interval_s = 5, .base_ts = DAY_TS + 86400, .record_size = sizeof(rec_home_t)};
    fwrite(&hdr, sizeof(hdr), 1, f);

    for (size_t i = 0; i < SLOTS; i++) {
        rec_home_t r;
        history_fill_sentinel(KIND_HOME, &r);
        r.heat_power_w = v3_defrost(i) == 1 ? -1500 : 4000;
        r.elec_power_w = 900;
        r.dhw_running  = i >= 380 && i < 450;
        r.heating_running = !r.dhw_running;
        r.defrosting   = v3_defrost(i);
        fwrite(&r, sizeof(r), 1, f);
    }
    fclose(f);
}

// The card's other contents, which the converter must leave alone: a room's directory, the totals
// cache, a stray file whose name only starts like a day, and a day that is not a history file.
static void write_card(void)
{
    remove_tree(IN_DIR);
    remove_tree(OUT_DIR);
    remove_tree(OUT2_DIR);
    mkdir(IN_DIR, 0755);
    mkdir(IN_DIR "/room-1", 0755);

    write_v2_day();
    write_v3_day();

    FILE *f = fopen(IN_DIR "/" BAD_DAY, "wb");
    fputs("not a history file at all", f);
    fclose(f);

    f = fopen(IN_DIR "/room-1/2026-01-01", "wb");
    fputs("room", f);
    fclose(f);

    f = fopen(IN_DIR "/cop-totals", "wb");
    fputs("cache", f);
    fclose(f);

    f = fopen(IN_DIR "/home-notes", "wb");
    fputs("not a day", f);
    fclose(f);
}

// --- Tests ----------------------------------------------------------------------

static uint16_t field(const uint8_t *rec, size_t f)
{
    uint16_t v;
    memcpy(&v, rec + f * 2, 2);
    return v;
}

static void test_converts_old_day(void)
{
    char   listing_before[512];
    size_t in_sizes[2];
    listing(IN_DIR, listing_before, sizeof(listing_before));
    uint8_t *v2_before = slurp(IN_DIR "/" V2_DAY, &in_sizes[0]);
    uint8_t *v3_before = slurp(IN_DIR "/" V3_DAY, &in_sizes[1]);

    char                     *report = NULL;
    size_t                    report_len;
    FILE                     *out    = open_memstream(&report, &report_len);
    history_convert_summary_t sum;
    int                       failed = history_convert_dir(IN_DIR, OUT_DIR, out, &sum);
    fclose(out);

    CHECK(failed == 1);
    CHECK(sum.converted == 1);
    CHECK(sum.unchanged == 1);
    CHECK(sum.failed == 1);
    CHECK(sum.trimmed == 1);

    // Reports what it converted, and the day it could not, by name.
    CHECK(strstr(report, V2_DAY "  converted (1000 slots); dropped a partial last record of 10 bytes") != NULL);
    CHECK(strstr(report, V3_DAY "  already converted") != NULL);
    CHECK(strstr(report, BAD_DAY "  FAILED: not a history file") != NULL);
    CHECK(strstr(report, "1 converted, 1 already converted, 1 failed") != NULL);
    free(report);

    // The input is exactly as it was.
    char   listing_after[512];
    size_t n;
    listing(IN_DIR, listing_after, sizeof(listing_after));
    CHECK(strcmp(listing_before, listing_after) == 0);
    uint8_t *v2_after = slurp(IN_DIR "/" V2_DAY, &n);
    CHECK(n == in_sizes[0] && memcmp(v2_before, v2_after, n) == 0);
    free(v2_after);
    uint8_t *v3_after = slurp(IN_DIR "/" V3_DAY, &n);
    CHECK(n == in_sizes[1] && memcmp(v3_before, v3_after, n) == 0);
    free(v3_after);
    free(v3_before);

    // The output has the two days and nothing else: not the failed day, no temporary file, none of
    // the card's other files.
    char out_listing[512];
    listing(OUT_DIR, out_listing, sizeof(out_listing));
    CHECK(strstr(out_listing, V2_DAY " ") != NULL);
    CHECK(strstr(out_listing, V3_DAY " ") != NULL);
    CHECK(strstr(out_listing, BAD_DAY) == NULL);
    CHECK(strstr(out_listing, ".tmp") == NULL);
    CHECK(strstr(out_listing, "room-1") == NULL);
    CHECK(strstr(out_listing, "cop-totals") == NULL);
    CHECK(strstr(out_listing, "home-notes") == NULL);

    // The converted day: the same header bar version and size, and every value where it was.
    uint8_t *conv = slurp(OUT_DIR "/" V2_DAY, &n);
    CHECK(conv != NULL);
    CHECK(n == sizeof(history_header_t) + SLOTS * sizeof(rec_home_t));
    if (!conv || n != sizeof(history_header_t) + SLOTS * sizeof(rec_home_t)) {
        free(conv);
        free(v2_before);
        return;
    }

    history_header_t in_hdr, out_hdr;
    memcpy(&in_hdr, v2_before, sizeof(in_hdr));
    memcpy(&out_hdr, conv, sizeof(out_hdr));
    CHECK(history_check_header(&out_hdr, KIND_HOME) == HISTORY_HEADER_OK);
    CHECK(out_hdr.version == HISTORY_HOME_VERSION);
    CHECK(out_hdr.record_size == 40);
    CHECK(out_hdr.base_ts == in_hdr.base_ts);
    CHECK(out_hdr.interval_s == in_hdr.interval_s);

    size_t mismatched = 0, defrost_known = 0, spare_known = 0;
    for (size_t i = 0; i < SLOTS; i++) {
        const uint8_t *rec = conv + sizeof(out_hdr) + i * sizeof(rec_home_t);
        for (size_t f = 0; f < HISTORY_HOME_V2_RECORD_SIZE / 2; f++) {
            if (field(rec, f) != v2_value(i, f)) {
                mismatched++;
            }
        }
        if (!history_field_is_null(KIND_HOME, 14, field(rec, 14))) {
            defrost_known++;
        }
        for (size_t f = 15; f < history_field_count(KIND_HOME); f++) {
            if (!history_field_is_null(KIND_HOME, f, field(rec, f))) {
                spare_known++;
            }
        }
    }
    CHECK(mismatched == 0);
    CHECK(defrost_known == 0); // unknown, not "not defrosting"
    CHECK(spare_known == 0);
    free(conv);
    free(v2_before);
}

static void test_defrost_reads_back(void)
{
    // The version 3 day went through unchanged, defrost flag and all.
    CHECK(same_bytes(IN_DIR "/" V3_DAY, OUT_DIR "/" V3_DAY));

    size_t   n;
    uint8_t *day = slurp(OUT_DIR "/" V3_DAY, &n);
    CHECK(day != NULL && n == sizeof(history_header_t) + SLOTS * sizeof(rec_home_t));
    if (!day || n != sizeof(history_header_t) + SLOTS * sizeof(rec_home_t)) {
        free(day);
        return;
    }
    size_t on = 0, off = 0, unknown = 0, wrong = 0, during_dhw = 0;

    for (size_t i = 0; i < SLOTS; i++) {
        const uint8_t *rec = day + sizeof(history_header_t) + i * sizeof(rec_home_t);
        uint16_t       raw = field(rec, offsetof(rec_home_t, defrosting) / 2);
        rec_home_t     r;
        memcpy(&r, rec, sizeof(r));

        if (history_field_is_null(KIND_HOME, 14, raw)) {
            unknown++;
        } else if ((int16_t)raw == 1) {
            on++;
            during_dhw += r.dhw_running == 1;
        } else {
            off++;
        }
        if ((int16_t)raw != v3_defrost(i)) {
            wrong++;
        }
    }
    CHECK(wrong == 0);
    CHECK(on == 80);
    CHECK(unknown == 50);
    CHECK(off == SLOTS - 80 - 50);
    CHECK(during_dhw == 20); // the second defrost happened inside a hot water run
    free(day);
}

static void test_rerun_changes_nothing(void)
{
    char                     *report = NULL;
    size_t                    report_len;
    FILE                     *out    = open_memstream(&report, &report_len);
    history_convert_summary_t sum;
    int                       failed = history_convert_dir(OUT_DIR, OUT2_DIR, out, &sum);
    fclose(out);

    CHECK(failed == 0);
    CHECK(sum.converted == 0);
    CHECK(sum.unchanged == 2);
    CHECK(sum.failed == 0);
    CHECK(same_bytes(OUT_DIR "/" V2_DAY, OUT2_DIR "/" V2_DAY));
    CHECK(same_bytes(OUT_DIR "/" V3_DAY, OUT2_DIR "/" V3_DAY));
    free(report);

    // And again into a directory that already holds the output, as a user re-running it would.
    out    = open_memstream(&report, &report_len);
    failed = history_convert_dir(OUT_DIR, OUT2_DIR, out, &sum);
    fclose(out);
    CHECK(failed == 0);
    CHECK(same_bytes(OUT_DIR "/" V2_DAY, OUT2_DIR "/" V2_DAY));
    free(report);
}

static void test_refuses_in_place(void)
{
    size_t   n_before, n_after;
    uint8_t *before = slurp(IN_DIR "/" V2_DAY, &n_before);

    char                     *report = NULL;
    size_t                    report_len;
    FILE                     *out = open_memstream(&report, &report_len);
    history_convert_summary_t sum;

    CHECK(history_convert_dir(IN_DIR, IN_DIR, out, &sum) == -1);
    CHECK(history_convert_dir(IN_DIR, IN_DIR "/.", out, &sum) == -1);
    CHECK(history_convert_dir(IN_DIR, "./" IN_DIR "/../" IN_DIR, out, &sum) == -1);
    fclose(out);
    CHECK(strstr(report, "Refusing to convert in place") != NULL);
    free(report);

    uint8_t *after = slurp(IN_DIR "/" V2_DAY, &n_after);
    CHECK(n_before == n_after && memcmp(before, after, n_before) == 0);
    CHECK(!exists(IN_DIR "/." V2_DAY ".tmp"));
    free(before);
    free(after);
}

static void test_check(void)
{
    char                     *report = NULL;
    size_t                    report_len;
    FILE                     *out = open_memstream(&report, &report_len);
    history_convert_summary_t sum;

    CHECK(history_convert_check_dir(IN_DIR, out, &sum) == 2);
    CHECK(sum.unchanged == 1 && sum.converted == 1 && sum.failed == 1);
    CHECK(history_convert_check_dir(OUT_DIR, out, &sum) == 0);
    CHECK(sum.unchanged == 2 && sum.converted == 0 && sum.failed == 0);
    fclose(out);
    CHECK(strstr(report, V2_DAY "  OLD FORMAT, needs converting") != NULL);
    free(report);
}

// The header check every reader in the firmware relies on.
static void test_header_check(void)
{
    history_header_t h = {.magic = HISTORY_MAGIC, .version = HISTORY_HOME_VERSION, .sensor_kind = KIND_HOME,
                          .interval_s = 5, .record_size = sizeof(rec_home_t)};
    CHECK(history_check_header(&h, KIND_HOME) == HISTORY_HEADER_OK);
    CHECK(history_check_header(&h, KIND_ROOM) == HISTORY_HEADER_BAD);

    h.version     = HISTORY_HOME_V2_VERSION;
    h.record_size = HISTORY_HOME_V2_RECORD_SIZE;
    CHECK(history_check_header(&h, KIND_HOME) == HISTORY_HEADER_OLD_FORMAT);

    h.record_size = sizeof(rec_home_t); // version 2 with the new size: neither
    CHECK(history_check_header(&h, KIND_HOME) == HISTORY_HEADER_BAD);

    // Rooms and radiators kept version 2 and their sizes, so their files from before still read.
    history_header_t room = {.magic = HISTORY_MAGIC, .version = 2, .sensor_kind = KIND_ROOM, .interval_s = 5,
                             .record_size = sizeof(rec_room_t)};
    CHECK(history_check_header(&room, KIND_ROOM) == HISTORY_HEADER_OK);
    history_header_t rad = {.magic = HISTORY_MAGIC, .version = 2, .sensor_kind = KIND_RADIATOR, .interval_s = 5,
                            .record_size = sizeof(rec_radiator_t)};
    CHECK(history_check_header(&rad, KIND_RADIATOR) == HISTORY_HEADER_OK);

    // The new fields are signed, so their sentinel is INT16_MIN; the old unsigned ones are where
    // they were.
    CHECK(history_unsigned_mask(KIND_HOME) == 0x0610);
    CHECK(history_field_count(KIND_HOME) == 20);
}

int main(void)
{
    write_card();

    test_header_check();
    test_converts_old_day();
    test_defrost_reads_back();
    test_rerun_changes_nothing();
    test_refuses_in_place();
    test_check();

    remove_tree(IN_DIR);
    remove_tree(OUT_DIR);
    remove_tree(OUT2_DIR);

    printf("test_convert: %d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
