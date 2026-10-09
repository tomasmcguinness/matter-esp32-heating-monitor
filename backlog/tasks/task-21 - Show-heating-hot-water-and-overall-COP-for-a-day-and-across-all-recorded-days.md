---
id: TASK-21
title: 'Show heating, hot water and overall COP for a day and across all recorded days'
status: In Progress
assignee: []
created_date: '2026-10-09 08:35'
updated_date: '2026-10-09 08:38'
labels:
  - firmware
  - web-app
dependencies:
  - TASK-20
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The History page shows one COP for the day, which blends space heating and hot water - two jobs at very different flow temperatures - so it cannot say whether either is performing well. Each home history slot now records whether the heat pump was heating or making hot water (TASK-20), so the energy can be split.

Show three COPs: an average for heating, an average for hot water, and SCOP, the overall average.

Decisions agreed with the user:
- Period: both the day being viewed and all recorded days.
- Display: cards on the History page. The Home page's live COP is unchanged.
- Each figure is heat energy divided by electrical energy over the slots it covers, not a mean of the instantaneous COP.
- A slot is hot water if recorded as hot water, otherwise heating if recorded as heating, otherwise neither. Energy in neither mode (standby, pump overrun) counts toward the overall figure only.
- Days recorded before the mode was logged contribute to the overall figure only.

Not included: a season boundary for the all-days figures (they cover everything on the card), and MQTT / Home Assistant.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The History page shows Heating COP, Hot water COP and Overall COP for the day being viewed
- [ ] #2 The History page shows Heating COP, Hot water COP and SCOP across all recorded days, with how many days and since when
- [ ] #3 Each COP is heat energy over electrical energy for its slots, and shows a dash when there is no electricity to divide by
- [ ] #4 Energy recorded in neither mode is counted in the overall figures and in neither of the other two
- [ ] #5 A day recorded before the mode was logged shows its overall COP as before and a dash for the other two
- [ ] #6 The all-days figures do not require re-reading every day's file on each request, and the stored per-day totals can be deleted and are rebuilt
- [ ] #7 The dates listing does not show the totals file as a date
- [ ] #8 With no SD card the page shows its existing warning and no all-days cards
- [x] #9 firmware/CLAUDE.md describes the new response fields, the totals endpoint and the totals file
- [x] #10 Firmware builds and the touched web app files pass lint and type-check
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented 2026-10-09. New firmware/main/storage/history_totals.{h,c}: history_totals_add() holds the mode-split energy arithmetic, used per slot by stream_file() (response tail gains `modes`) and by history_totals_scan_file(). Completed days are summarised into /sdcard/cop-totals (8-byte header, 64-byte record per day) by history_totals_backfill_one(), called from the logger's flush task after each flush, one day per minute. GET /api/history/totals sums the cache plus a live scan of today; it is routed inside the /api/history/* wildcard. Flush task stack raised 4096 -> 6144. Web app: History.tsx shows three day cards and three all-days cards.

Verified: idf.py build links cleanly, tsc -b passes, eslint clean on History.tsx and historyData.ts. NOT verified: nothing flashed. Criteria 1-8 are untested on the device - in particular the backfill has never run against a real card, the 6144 flush stack is an estimate rather than a measured high-water mark, and no COP figure has been checked against hand-summed energy.

Depends on TASK-20 being flashed: heating and hot water figures only exist for days recorded after that.
<!-- SECTION:NOTES:END -->
