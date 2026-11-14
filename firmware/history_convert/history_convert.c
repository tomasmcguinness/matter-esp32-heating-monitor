// The offline history converter. See history_convert.h.
//
// Every check here is against history_format.h, the header the firmware is built from: the old
// layout is HISTORY_HOME_V2_RECORD_SIZE bytes, statically asserted there to be the prefix of
// today's rec_home_t, and the new fields are filled by history_fill_sentinel(), the same function
// the logger pads missed slots with. A converted day therefore reads exactly as one the firmware
// recorded with no defrost endpoint: every old value where it was, defrost unknown.

#include "history_convert.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "history_format.h"

#define HOME_PREFIX     "home-"
#define HOME_PREFIX_LEN 5
#define DATE_LEN        10 // YYYY-MM-DD

// home-YYYY-MM-DD, exactly as history_paths.c names a home day. Anything else in the card's root
// -- the room and radiator directories, the subscription log, the totals cache -- is not ours.
static bool is_home_day(const char *name)
{
    if (strncmp(name, HOME_PREFIX, HOME_PREFIX_LEN) != 0 || strlen(name) != HOME_PREFIX_LEN + DATE_LEN) {
        return false;
    }

    const char *date = name + HOME_PREFIX_LEN;

    for (int i = 0; i < DATE_LEN; i++) {
        bool dash = i == 4 || i == 7;
        if (dash ? date[i] != '-' : (date[i] < '0' || date[i] > '9')) {
            return false;
        }
    }
    return true;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// The home days in `dir`, sorted, so the report reads in date order. NULL if `dir` can't be read.
static char **list_home_days(const char *dir, size_t *count)
{
    DIR *d = opendir(dir);
    if (!d) {
        return NULL;
    }

    size_t         n     = 0;
    size_t         cap   = 64;
    char         **names = malloc(cap * sizeof(char *));
    struct dirent *ent;

    while (names && (ent = readdir(d)) != NULL) {
        if (!is_home_day(ent->d_name)) {
            continue;
        }

        char        path[4096];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        if (n == cap) {
            cap *= 2;
            char **grown = realloc(names, cap * sizeof(char *));
            if (!grown) {
                break;
            }
            names = grown;
        }
        names[n++] = strdup(ent->d_name);
    }
    closedir(d);

    if (names) {
        qsort(names, n, sizeof(char *), compare_names);
    }
    *count = n;
    return names;
}

static void free_names(char **names, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

// The whole file, which is at most a few hundred kilobytes for a day at 1 s sampling.
static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    uint8_t *buf = NULL;
    long     len = -1;

    if (fseek(f, 0, SEEK_END) == 0) {
        len = ftell(f);
    }
    if (len >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc(len > 0 ? (size_t)len : 1);
        if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
            free(buf);
            buf = NULL;
        }
    }

    fclose(f);
    *size = buf ? (size_t)len : 0;
    return buf;
}

// Why a header is not one this converter can work with, for the report.
static const char *header_problem(const history_header_t *hdr)
{
    if (hdr->magic != HISTORY_MAGIC) {
        return "not a history file (bad magic)";
    }
    if (hdr->sensor_kind != KIND_HOME) {
        return "not a home series";
    }
    if (hdr->interval_s == 0) {
        return "sampling interval of 0";
    }
    return "unknown version or record size";
}

// The version 3 day for a version 2 one: the same header bar version and record size, and every
// whole record's 28 bytes followed by the sentinel in each new field. A partial record at the end
// -- the board losing power mid-write -- is dropped; the firmware's readers already ignore one,
// since they count slots as whole records.
static uint8_t *convert_v2(const uint8_t *in, size_t in_size, size_t *out_size, size_t *slots, size_t *trimmed)
{
    *slots   = (in_size - sizeof(history_header_t)) / HISTORY_HOME_V2_RECORD_SIZE;
    *trimmed = (in_size - sizeof(history_header_t)) % HISTORY_HOME_V2_RECORD_SIZE;
    *out_size = sizeof(history_header_t) + *slots * sizeof(rec_home_t);

    uint8_t *out = malloc(*out_size);
    if (!out) {
        return NULL;
    }

    history_header_t hdr;
    memcpy(&hdr, in, sizeof(hdr));
    hdr.version     = HISTORY_HOME_VERSION;
    hdr.record_size = sizeof(rec_home_t);
    memcpy(out, &hdr, sizeof(hdr));

    for (size_t i = 0; i < *slots; i++) {
        rec_home_t rec;
        history_fill_sentinel(KIND_HOME, &rec);
        memcpy(&rec, in + sizeof(hdr) + i * HISTORY_HOME_V2_RECORD_SIZE, HISTORY_HOME_V2_RECORD_SIZE);
        memcpy(out + sizeof(hdr) + i * sizeof(rec_home_t), &rec, sizeof(rec));
    }
    return out;
}

// Checks what was written against the input it came from, independently of how convert_v2() built
// it: the header unchanged bar version and record size, the same number of slots, each old record
// intact at the start of its new one, and nothing but the sentinel after it.
static const char *verify_v2(const uint8_t *in, size_t in_size, const uint8_t *out, size_t out_size)
{
    history_header_t in_hdr, out_hdr;
    memcpy(&in_hdr, in, sizeof(in_hdr));
    if (out_size < sizeof(out_hdr)) {
        return "output shorter than a header";
    }
    memcpy(&out_hdr, out, sizeof(out_hdr));

    if (history_check_header(&out_hdr, KIND_HOME) != HISTORY_HEADER_OK) {
        return "output header is not the current format";
    }
    if (out_hdr.magic != in_hdr.magic || out_hdr.sensor_kind != in_hdr.sensor_kind ||
        out_hdr.interval_s != in_hdr.interval_s || out_hdr.base_ts != in_hdr.base_ts ||
        out_hdr.reserved != in_hdr.reserved) {
        return "output header differs from input";
    }

    size_t in_slots = (in_size - sizeof(in_hdr)) / HISTORY_HOME_V2_RECORD_SIZE;
    if (out_size != sizeof(out_hdr) + in_slots * sizeof(rec_home_t)) {
        return "output has a different number of slots";
    }

    rec_home_t blank;
    history_fill_sentinel(KIND_HOME, &blank);
    const size_t added = sizeof(rec_home_t) - HISTORY_HOME_V2_RECORD_SIZE;

    for (size_t i = 0; i < in_slots; i++) {
        const uint8_t *old_rec = in + sizeof(in_hdr) + i * HISTORY_HOME_V2_RECORD_SIZE;
        const uint8_t *new_rec = out + sizeof(out_hdr) + i * sizeof(rec_home_t);

        if (memcmp(old_rec, new_rec, HISTORY_HOME_V2_RECORD_SIZE) != 0) {
            return "a value changed";
        }
        if (memcmp(new_rec + HISTORY_HOME_V2_RECORD_SIZE, (const uint8_t *)&blank + HISTORY_HOME_V2_RECORD_SIZE,
                   added) != 0) {
            return "a new field is not the sentinel";
        }
    }
    return NULL;
}

// Writes `data` to `dir/.name.tmp`, flushed to the disk, and returns that path in `tmp`.
static bool write_temp(const char *dir, const char *name, const uint8_t *data, size_t size, char *tmp, size_t tmp_len)
{
    snprintf(tmp, tmp_len, "%s/.%s.tmp", dir, name);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }

    bool ok = fwrite(data, 1, size, f) == size && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;

    if (!ok) {
        unlink(tmp);
    }
    return ok;
}

typedef enum { DAY_CONVERTED, DAY_UNCHANGED, DAY_FAILED } day_result_t;

static day_result_t convert_day(const char *in_dir, const char *out_dir, const char *name, FILE *report,
                                bool *trimmed_out)
{
    char in_path[4096], out_path[4096], tmp[4096];
    snprintf(in_path, sizeof(in_path), "%s/%s", in_dir, name);
    snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);
    *trimmed_out = false;

    size_t   in_size = 0;
    uint8_t *in      = read_file(in_path, &in_size);
    if (!in) {
        fprintf(report, "%s  FAILED: cannot read it\n", name);
        return DAY_FAILED;
    }

    if (in_size < sizeof(history_header_t)) {
        fprintf(report, "%s  FAILED: shorter than a header\n", name);
        free(in);
        return DAY_FAILED;
    }

    history_header_t hdr;
    memcpy(&hdr, in, sizeof(hdr));

    day_result_t result;
    uint8_t     *out      = NULL;
    size_t       out_size = 0;
    size_t       slots    = 0;
    size_t       trimmed  = 0;

    switch (history_check_header(&hdr, KIND_HOME)) {
    case HISTORY_HEADER_OK:
        // Already converted. Copied exactly as it is -- including any partial last record -- so a
        // second run over converted output changes nothing at all.
        out      = in;
        out_size = in_size;
        slots    = (in_size - sizeof(hdr)) / sizeof(rec_home_t);
        result   = DAY_UNCHANGED;
        break;

    case HISTORY_HEADER_OLD_FORMAT:
        out = convert_v2(in, in_size, &out_size, &slots, &trimmed);
        if (!out) {
            fprintf(report, "%s  FAILED: out of memory\n", name);
            free(in);
            return DAY_FAILED;
        }
        result = DAY_CONVERTED;
        break;

    case HISTORY_HEADER_BAD:
    default:
        fprintf(report, "%s  FAILED: %s\n", name, header_problem(&hdr));
        free(in);
        return DAY_FAILED;
    }

    const char *problem = NULL;

    if (!write_temp(out_dir, name, out, out_size, tmp, sizeof(tmp))) {
        problem = "cannot write the output";
    } else {
        size_t   back_size = 0;
        uint8_t *back      = read_file(tmp, &back_size);

        if (!back) {
            problem = "cannot read the output back";
        } else if (result == DAY_UNCHANGED) {
            if (back_size != in_size || memcmp(back, in, in_size) != 0) {
                problem = "copy differs from input";
            }
        } else {
            problem = verify_v2(in, in_size, back, back_size);
        }
        free(back);

        if (problem) {
            unlink(tmp);
        } else if (rename(tmp, out_path) != 0) {
            unlink(tmp);
            problem = "cannot move the output into place";
        }
    }

    if (out != in) {
        free(out);
    }
    free(in);

    if (problem) {
        fprintf(report, "%s  FAILED: %s; nothing written for this day\n", name, problem);
        return DAY_FAILED;
    }

    if (result == DAY_UNCHANGED) {
        fprintf(report, "%s  already converted, copied unchanged (%zu slots)\n", name, slots);
    } else if (trimmed) {
        fprintf(report, "%s  converted (%zu slots); dropped a partial last record of %zu bytes\n", name, slots, trimmed);
        *trimmed_out = true;
    } else {
        fprintf(report, "%s  converted (%zu slots)\n", name, slots);
    }
    return result;
}

static bool same_directory(const char *a, const char *b)
{
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

int history_convert_dir(const char *in_dir, const char *out_dir, FILE *report, history_convert_summary_t *summary)
{
    memset(summary, 0, sizeof(*summary));

    struct stat st;
    if (stat(in_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(report, "Cannot read %s: not a directory\n", in_dir);
        return -1;
    }

    if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(report, "Cannot create %s: %s\n", out_dir, strerror(errno));
        return -1;
    }

    // The one thing this must never do is write over what it is reading.
    if (same_directory(in_dir, out_dir)) {
        fprintf(report, "Refusing to convert in place: %s and %s are the same directory. "
                        "Give an empty directory for the output.\n",
                in_dir, out_dir);
        return -1;
    }

    size_t count = 0;
    char **names = list_home_days(in_dir, &count);
    if (!names) {
        fprintf(report, "Cannot read %s\n", in_dir);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        bool trimmed = false;

        switch (convert_day(in_dir, out_dir, names[i], report, &trimmed)) {
        case DAY_CONVERTED: summary->converted++; break;
        case DAY_UNCHANGED: summary->unchanged++; break;
        case DAY_FAILED:    summary->failed++;    break;
        }
        if (trimmed) {
            summary->trimmed++;
        }
    }
    free_names(names, count);

    fprintf(report, "\n%u converted, %u already converted, %u failed", summary->converted, summary->unchanged,
            summary->failed);
    if (summary->trimmed) {
        fprintf(report, " (%u with a partial last record dropped)", summary->trimmed);
    }
    fprintf(report, ".\n");

    if (summary->failed) {
        fprintf(report, "The days that failed were not written to %s. Their originals are untouched.\n", out_dir);
    }

    return (int)summary->failed;
}

int history_convert_check_dir(const char *dir, FILE *report, history_convert_summary_t *summary)
{
    memset(summary, 0, sizeof(*summary));

    size_t count = 0;
    char **names = list_home_days(dir, &count);
    if (!names) {
        fprintf(report, "Cannot read %s\n", dir);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);

        FILE            *f = fopen(path, "rb");
        history_header_t hdr;
        bool             have = f && fread(&hdr, sizeof(hdr), 1, f) == 1;
        if (f) {
            fclose(f);
        }

        switch (have ? history_check_header(&hdr, KIND_HOME) : HISTORY_HEADER_BAD) {
        case HISTORY_HEADER_OK:
            fprintf(report, "%s  current format\n", names[i]);
            summary->unchanged++;
            break;
        case HISTORY_HEADER_OLD_FORMAT:
            fprintf(report, "%s  OLD FORMAT, needs converting\n", names[i]);
            summary->converted++;
            break;
        default:
            fprintf(report, "%s  UNREADABLE: %s\n", names[i], have ? header_problem(&hdr) : "no header");
            summary->failed++;
            break;
        }
    }
    free_names(names, count);

    fprintf(report, "\n%u in the current format, %u need converting, %u unreadable.\n", summary->unchanged,
            summary->converted, summary->failed);

    return (int)(summary->converted + summary->failed);
}
