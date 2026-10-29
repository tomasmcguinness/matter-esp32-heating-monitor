#include "timezones.h"

#include <string.h>

// Abbreviations are alphabetic throughout: newlib's tzset() is not relied on to parse the
// quoted <+03> form. They are only labels -- the offsets are what matter.
static const tz_entry_t ZONES[] = {
    {"UTC",                 "UTC0"},

    {"Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin",       "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon",       "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Atlantic/Reykjavik",  "GMT0"},
    {"Europe/Amsterdam",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels",     "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen",   "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest",    "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki",     "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kyiv",         "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul",     "TRT-3"},
    {"Europe/Moscow",       "MSK-3"},

    {"America/St_Johns",    "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Halifax",     "AST4ADT,M3.2.0,M11.1.0"},
    {"America/New_York",    "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto",     "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago",     "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Winnipeg",    "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "CST6"},
    {"America/Denver",      "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Edmonton",    "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix",     "MST7"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Vancouver",   "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage",   "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu",    "HST10"},
    {"America/Sao_Paulo",   "BRT3"},

    {"Africa/Johannesburg", "SAST-2"},
    {"Asia/Dubai",          "GST-4"},
    {"Asia/Kolkata",        "IST-5:30"},
    {"Asia/Singapore",      "SGT-8"},
    {"Asia/Hong_Kong",      "HKT-8"},
    {"Asia/Shanghai",       "CST-8"},
    {"Asia/Seoul",          "KST-9"},
    {"Asia/Tokyo",          "JST-9"},

    {"Australia/Perth",     "AWST-8"},
    {"Australia/Darwin",    "ACST-9:30"},
    {"Australia/Adelaide",  "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane",  "AEST-10"},
    {"Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Hobart",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

#define ZONE_COUNT (sizeof(ZONES) / sizeof(ZONES[0]))

const char *timezones_posix_for(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < ZONE_COUNT; i++) {
        if (strcmp(ZONES[i].name, name) == 0) {
            return ZONES[i].posix;
        }
    }
    return NULL;
}

size_t timezones_count(void)
{
    return ZONE_COUNT;
}

const tz_entry_t *timezones_at(size_t index)
{
    return index < ZONE_COUNT ? &ZONES[index] : NULL;
}
