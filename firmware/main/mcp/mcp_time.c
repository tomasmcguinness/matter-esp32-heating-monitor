#include "mcp_time.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Days from 1970-01-01 to a civil date, by Howard Hinnant's algorithm. newlib has no timegm().
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t utc_seconds(int y, int mo, int d, int h, int mi, int s)
{
    return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

// Reads exactly `digits` decimal digits, advancing *p.
static bool read_digits(const char **p, int digits, int *out)
{
    int value = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit((unsigned char)(*p)[i])) {
            return false;
        }
        value = value * 10 + ((*p)[i] - '0');
    }
    *p  += digits;
    *out = value;
    return true;
}

bool mcp_time_parse(const char *text, bool utc, uint32_t *out)
{
    if (!text) {
        return false;
    }

    const char *p = text;
    int         y, mo, d, h = 0, mi = 0, s = 0;

    if (!read_digits(&p, 4, &y) || *p++ != '-' || !read_digits(&p, 2, &mo) || *p++ != '-' ||
        !read_digits(&p, 2, &d)) {
        return false;
    }

    if (*p == 'T' || *p == 't' || *p == ' ') {
        p++;
        if (!read_digits(&p, 2, &h) || *p++ != ':' || !read_digits(&p, 2, &mi)) {
            return false;
        }
        if (*p == ':') {
            p++;
            if (!read_digits(&p, 2, &s)) {
                return false;
            }
            if (*p == '.') {
                p++;
                while (isdigit((unsigned char)*p)) {
                    p++;
                }
            }
        }
    }

    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
        return false;
    }

    int64_t result;

    if (*p == 'Z' || *p == 'z') {
        p++;
        result = utc_seconds(y, mo, d, h, mi, s);
    } else if (*p == '+' || *p == '-') {
        int sign = (*p++ == '-') ? -1 : 1;
        int oh, om = 0;
        if (!read_digits(&p, 2, &oh)) {
            return false;
        }
        if (*p == ':') {
            p++;
        }
        if (isdigit((unsigned char)*p) && !read_digits(&p, 2, &om)) {
            return false;
        }
        result = utc_seconds(y, mo, d, h, mi, s) - sign * (oh * 3600 + om * 60);
    } else if (utc) {
        result = utc_seconds(y, mo, d, h, mi, s);
    } else {
        struct tm tm = {0};
        tm.tm_year   = y - 1900;
        tm.tm_mon    = mo - 1;
        tm.tm_mday   = d;
        tm.tm_hour   = h;
        tm.tm_min    = mi;
        tm.tm_sec    = s;
        tm.tm_isdst  = -1; // let TZ decide which side of a clock change this is
        result       = (int64_t)mktime(&tm);
    }

    if (*p != '\0' || result < 0 || result > UINT32_MAX) {
        return false;
    }

    *out = (uint32_t)result;
    return true;
}

void mcp_time_format(uint32_t ts, bool utc, char *buf, size_t len)
{
    time_t    t = (time_t)ts;
    struct tm tm;

    if (utc) {
        gmtime_r(&t, &tm);
        snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return;
    }

    localtime_r(&t, &tm);

    // The offset is the local wall clock read as if it were UTC, less the instant itself.
    int64_t offset = utc_seconds(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                                 tm.tm_min, tm.tm_sec) - (int64_t)ts;
    int64_t abs_offset = offset < 0 ? -offset : offset;

    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, offset < 0 ? '-' : '+',
             (int)(abs_offset / 3600), (int)((abs_offset % 3600) / 60));
}
