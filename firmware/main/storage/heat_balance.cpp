#include "heat_balance.h"
#include "heat_balance_api.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "heat_balance_core.h"
#include "history_paths.h"
#include "sd_card.h"
#include "time_sync.h"

static const char *TAG = "heat_balance";

#define BALANCE_DIR SD_CARD_MOUNT_POINT "/balance"
#define BALANCE_MAGIC 0x4E4C4142u // "BALN", little-endian

// Version 2 stores the bucket length in the header. Version 1 -- a few hours of 2026-11-24 only --
// had hourly buckets and 0 there; it is still read, as hourly.
#define BALANCE_VERSION 2
#define BUCKET_S 600

// Completed buckets waiting for the flush task. It runs once a minute, so one is normally enough; the
// rest cover the card being unwritable for a while. Past that the oldest is dropped.
#define PENDING_MAX 8

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t record_size;
    uint16_t bucket_s; // 0 in version 1, whose buckets were an hour
} balance_header_t;

static_assert(sizeof(balance_header_t) == 8, "balance_header_t must stay 8 bytes");

static SemaphoreHandle_t s_mutex;
static hb_bucket_t s_current;              // the bucket being added up
static hb_record_t s_pending[PENDING_MAX]; // completed buckets not yet on the card
static size_t s_pending_count;

esp_err_t heat_balance_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

// The start of the 10-minute bucket of local clock time containing ts: :00, :10, :20 and so on.
// Local, so the buckets line up with the clock on the wall (a zone with a 30 or 45 minute offset
// still lands on whole tens of local minutes) and a day file holds that day's 138 to 150 of them.
static uint32_t local_bucket_start(uint32_t ts)
{
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    return ts - (uint32_t)((tm.tm_min % (BUCKET_S / 60)) * 60 + tm.tm_sec);
}

void heat_balance_sample(const home_manager_t *home, uint32_t now, uint32_t seconds)
{
    if (!s_mutex)
    {
        return;
    }

    // One read per field, like build_record(): attribute_data_cb writes these from the CHIP task,
    // and a torn 64-bit read costs at most one odd sample.
    hb_sample_t s = {};
    s.heating = home->has_heating_running && home->heating_running;
    s.dhw = home->has_dhw_running && home->dhw_running;
    s.defrosting = home_is_defrosting(home);
    s.heat_meter_known = home->has_heat_meter_power;
    s.heat_meter_w = (int32_t)(home->heat_meter_power_mw / 1000);
    s.radiators_known = home->has_total_radiator_output;
    s.radiators_w = home->total_radiator_output;
    // With no manifolds there is no UFH to wait for: it is a known 0 W.
    s.ufh_known = home->ufh_manifold_count == 0 || home->has_ufh_output;
    s.ufh_w = home->ufh_manifold_count == 0 ? 0 : home->total_ufh_output;

    hb_bucket_t done;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (hb_add(&s_current, local_bucket_start(now), seconds, &s, &done))
    {
        if (s_pending_count == PENDING_MAX)
        {
            memmove(&s_pending[0], &s_pending[1], (PENDING_MAX - 1) * sizeof(hb_record_t));
            s_pending_count--;
        }

        s_pending[s_pending_count++] = hb_record(&done);
    }

    xSemaphoreGive(s_mutex);
}

static bool path_for(uint32_t ts, char *out, size_t len)
{
    char date[16];
    local_date_string(ts, date, sizeof(date));
    return snprintf(out, len, "%s/%s", BALANCE_DIR, date) < (int)len;
}

// The bucket length a file's header describes, or 0 if the header is not one this firmware reads.
static uint16_t header_bucket_s(const balance_header_t *hdr)
{
    if (hdr->magic != BALANCE_MAGIC || hdr->record_size != sizeof(hb_record_t))
    {
        return 0;
    }

    if (hdr->version == 1)
    {
        return 3600;
    }

    return hdr->version == BALANCE_VERSION ? hdr->bucket_s : 0;
}

// Appends one record to its day's file, creating the file with its header if need be. A bucket
// already in the file (the last record's start is not before it) is not written again. A day file
// with other buckets -- the hourly version 1 -- is not appended to: the rest of that day is dropped
// rather than mixed in.
static bool append_record(const hb_record_t *rec)
{
    char path[HISTORY_PATH_MAX];

    if (!path_for(rec->start, path, sizeof(path)))
    {
        return false;
    }

    FILE *f = fopen(path, "r+b");

    if (f)
    {
        balance_header_t hdr;

        if (fread(&hdr, sizeof(hdr), 1, f) != 1 || header_bucket_s(&hdr) != BUCKET_S)
        {
            ESP_LOGW(TAG, "%s has another layout; not appending", path);
            fclose(f);
            return true; // dropped rather than retried forever
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);

        if (size >= (long)(sizeof(hdr) + sizeof(hb_record_t)))
        {
            hb_record_t last;
            fseek(f, size - (long)sizeof(hb_record_t), SEEK_SET);

            if (fread(&last, sizeof(last), 1, f) == 1 && last.start >= rec->start)
            {
                fclose(f);
                return true;
            }

            fseek(f, 0, SEEK_END);
        }
    }
    else
    {
        // EEXIST on every day but the first; fopen reports any real failure.
        mkdir(BALANCE_DIR, 0777);

        f = fopen(path, "w+b");

        if (!f)
        {
            ESP_LOGW(TAG, "Cannot open %s", path);
            return false;
        }

        balance_header_t hdr = {BALANCE_MAGIC, BALANCE_VERSION, (uint8_t)sizeof(hb_record_t), BUCKET_S};

        if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
        {
            fclose(f);
            return false;
        }
    }

    bool ok = fwrite(rec, sizeof(*rec), 1, f) == 1;
    fclose(f);

    if (ok)
    {
        ESP_LOGI(TAG, "Bucket at %lu: %u s counted, %u s unknown; meter %ld Wh, radiators %ld Wh, UFH %ld Wh",
                 (unsigned long)rec->start, rec->counted_s, rec->unknown_s, (long)rec->heat_meter_wh,
                 (long)rec->radiators_wh, (long)rec->ufh_wh);
    }

    return ok;
}

void heat_balance_flush(void)
{
    if (!s_mutex || !sd_card_available())
    {
        return;
    }

    hb_record_t batch[PENDING_MAX];
    size_t count;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    count = s_pending_count;
    memcpy(batch, s_pending, count * sizeof(hb_record_t));
    xSemaphoreGive(s_mutex);

    size_t written = 0;

    while (written < count && append_record(&batch[written]))
    {
        written++;
    }

    // Only what was written leaves the queue; a sample may have added another meanwhile.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memmove(&s_pending[0], &s_pending[written], (s_pending_count - written) * sizeof(hb_record_t));
    s_pending_count -= written;
    xSemaphoreGive(s_mutex);
}

static esp_err_t emit_bucket(httpd_req_t *req, const hb_record_t *rec, bool *first)
{
    char buf[200];
    snprintf(buf, sizeof(buf),
             "%s{\"start\":%lu,\"countedSeconds\":%u,\"unknownSeconds\":%u,\"heatMeterWh\":%ld,\"radiatorsWh\":%ld,\"ufhWh\":%ld}",
             *first ? "" : ",", (unsigned long)rec->start, rec->counted_s, rec->unknown_s, (long)rec->heat_meter_wh,
             (long)rec->radiators_wh, (long)rec->ufh_wh);
    *first = false;
    return httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
}

esp_err_t heat_balance_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (!sd_card_available())
    {
        return httpd_resp_sendstr(req, "{\"storage\":\"unavailable\"}");
    }

    char date[16] = {0};
    char raw[32];
    char query[64];
    size_t qlen = httpd_req_get_url_query_len(req);

    if (qlen > 0 && qlen < sizeof(query) && httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "date", raw, sizeof(raw)) == ESP_OK)
    {
        int y, m, d;

        if (strlen(raw) != 10 || sscanf(raw, "%4d-%2d-%2d", &y, &m, &d) != 3)
        {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "{\"error\":\"bad date\"}");
        }

        snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    }
    else if (clock_is_valid())
    {
        local_date_string((uint32_t)time(NULL), date, sizeof(date));
    }
    else
    {
        return httpd_resp_sendstr(req, "{\"storage\":\"ok\",\"bucketSeconds\":600,\"buckets\":[],\"current\":null}");
    }

    char path[HISTORY_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", BALANCE_DIR, date);

    // The file's own bucket length, so a version 1 (hourly) day is shown as what it is. A day with no
    // file is in this firmware's buckets.
    FILE *f = fopen(path, "rb");
    uint16_t bucket_s = BUCKET_S;
    bool readable = false;

    if (f)
    {
        balance_header_t hdr;

        if (fread(&hdr, sizeof(hdr), 1, f) == 1 && header_bucket_s(&hdr) != 0)
        {
            bucket_s = header_bucket_s(&hdr);
            readable = true;
        }
    }

    char head[96];
    snprintf(head, sizeof(head), "{\"storage\":\"ok\",\"date\":\"%s\",\"bucketSeconds\":%u,\"buckets\":[", date, bucket_s);
    esp_err_t err = httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

    bool first = true;
    uint32_t last_start = 0;

    if (f && readable)
    {
        hb_record_t rec;

        while (err == ESP_OK && fread(&rec, sizeof(rec), 1, f) == 1)
        {
            err = emit_bucket(req, &rec, &first);
            last_start = rec.start;
        }
    }

    if (f)
    {
        fclose(f);
    }

    // Buckets finished but not yet flushed, and the one in progress: today's view is current rather
    // than up to a minute (or a bucket) behind. Only when the day is in this firmware's buckets.
    hb_record_t pending[PENDING_MAX];
    size_t pending_count = 0;
    hb_bucket_t current = {};

    if (s_mutex && bucket_s == BUCKET_S)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        pending_count = s_pending_count;
        memcpy(pending, s_pending, pending_count * sizeof(hb_record_t));
        current = s_current;
        xSemaphoreGive(s_mutex);
    }

    for (size_t i = 0; i < pending_count && err == ESP_OK; i++)
    {
        char day[16];
        local_date_string(pending[i].start, day, sizeof(day));

        if (strcmp(day, date) == 0 && pending[i].start > last_start)
        {
            err = emit_bucket(req, &pending[i], &first);
        }
    }

    if (err == ESP_OK)
    {
        err = httpd_resp_send_chunk(req, "],\"current\":", HTTPD_RESP_USE_STRLEN);
    }

    char day[16] = {0};

    if (current.start != 0)
    {
        local_date_string(current.start, day, sizeof(day));
    }

    if (err == ESP_OK && current.start != 0 && strcmp(day, date) == 0)
    {
        hb_record_t rec = hb_record(&current);
        bool none = true;
        err = emit_bucket(req, &rec, &none);
    }
    else if (err == ESP_OK)
    {
        err = httpd_resp_send_chunk(req, "null", HTTPD_RESP_USE_STRLEN);
    }

    if (err == ESP_OK)
    {
        err = httpd_resp_send_chunk(req, "}", HTTPD_RESP_USE_STRLEN);
    }

    if (err != ESP_OK)
    {
        return err; // the client has gone
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}
