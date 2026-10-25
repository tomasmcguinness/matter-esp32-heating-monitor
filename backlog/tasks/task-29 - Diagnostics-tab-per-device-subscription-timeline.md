---
id: TASK-29
title: 'Diagnostics tab: per-device subscription timeline'
status: In Progress
assignee: []
created_date: '2026-10-16 18:43'
updated_date: '2026-10-16 18:50'
labels:
  - firmware
  - web-app
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The heat pump adapter keeps losing its subscription (e.g. 18:00-19:20 on 2026-10-16) and there is no record of when any device was or was not subscribed, how often it dropped, or why attempts failed. Record subscription transitions on the SD card and show them per device on a new Diagnostics page.

Design:
- Firmware module storage/subscription_log.{h,cpp}. The three subscription callbacks in app_main.cpp (established, terminated, subscribe failed) and unpair call subscription_log_record(); it only pushes onto a mutex-guarded RAM ring stamped with uptime (and wall time if the clock is valid), so no SD I/O happens on the CHIP event loop.
- The history flush task drains the ring once a minute into /sdcard/subs/YYYY-MM-DD: an 8-byte header and fixed 24-byte records (ts, node id, event, CHIP error code). Events taken before SNTP are converted from uptime once the clock is valid.
- Event kinds: boot, day start + one 'up at start' record per subscribed node (so every day file states where it starts), established, terminated (an established subscription ended), attempt failed (with error), removed (unpaired), alive (written every flush, so the reader can tell when the monitor itself was off).
- GET /api/history/subscriptions?date= is routed inside the existing /api/history/* wildcard (no new URI handler). It returns the day's events plus any not yet flushed, with error codes decoded to text; alive records are folded into lastAlive values rather than sent.
- Web app: Diagnostics.tsx at /diagnostics, a Diagnostics nav tab. Date toolbar; per device a 24h bar (subscribed / not subscribed / monitor off), connected %, drops, failed attempts, longest outage, and a list of outages with their failure reasons.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Every subscription established, ended and failed attempt is recorded on the SD card with its time, node and (for failures) CHIP error
- [ ] #2 Events taken before the clock is set are recorded with the right wall time once it is
- [ ] #3 Each day file states which nodes were subscribed at its start, so a day with no events still draws correctly
- [ ] #4 Periods when the monitor itself was off are distinguishable from a device being unsubscribed
- [x] #5 No SD I/O happens on the CHIP event loop and no new URI handler is registered
- [ ] #6 A Diagnostics tab shows, for a chosen day, a timeline per device of subscribed / not subscribed / monitor off, with drop and failure counts and an outage list with error reasons
- [x] #7 firmware/CLAUDE.md describes the log, its format and the endpoint
- [x] #8 Firmware builds; tsc passes and eslint is clean on files touched
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented 2026-10-16. Firmware: storage/subscription_log.{h,cpp} (RAM ring -> /sdcard/subs/YYYY-MM-DD, 24-byte records, DAY_START/UP_AT_START snapshot per day file, BOOT, ALIVE each flush); recorded from the established/terminated/failed callbacks and unpair_node() in app_main.cpp; subscription_log_init() before subscription_manager_init(); flushed from history_logger's flush task; GET /api/history/subscriptions routed in history_api.c's wildcard. Web: Diagnostics.tsx at /diagnostics plus nav tab. firmware/CLAUDE.md has a section on it.

Verified: idf.py reconfigure + build links and produces heating-monitor.bin (a new file under main/storage needs the reconfigure, since SRC_DIRS is globbed at configure time); tsc -b passes; eslint clean on Diagnostics.tsx and App.tsx; the timeline replay was run with node against a synthetic day (drop, two failed attempts with reasons, reconnect, reboot with a 5 min off gap, a never-subscribed node) and produced the expected segments, counts and outages.

NOT verified: nothing flashed. AC 1-4 and 6 are untested on the device, including the file writes, pre-SNTP time conversion and the endpoint's JSON.

Known limits: a failed attempt can be reported by both the termination (id 0) and failure callbacks, so failure counts may double. The ~20 s of re-subscription after every boot shows as a short outage on every node. The day boundary is the browser's local midnight; the device's is UK time.
<!-- SECTION:NOTES:END -->
