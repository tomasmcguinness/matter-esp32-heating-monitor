---
id: TASK-45
title: 'Record a heat balance every 10 minutes: heat meter vs radiators + UFH'
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-24 20:27'
updated_date: '2026-11-24 20:45'
labels:
  - history
  - firmware
  - web-ui
dependencies:
  - TASK-42.2
references:
  - firmware/main/storage/history_logger.cpp
  - firmware/main/storage/history_api.c
  - firmware/html_app/src/History.tsx
documentation:
  - >-
    ~/.claude/plans/we-need-to-rework-magical-lerdorf-of-6b56e096-56bd-42cc-b4bd-666b0018bccb.md
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The live figures let us compare the heat meter's output with the model's (radiators by dT50, UFH by calibrated loop flow x manifold dT), but radiator and UFH outputs are not recorded, so the gap can only be spot-checked. Rebuilding it in the browser from history would read ~20 day files per day and hold the device's single web server task for seconds. At the user's request (2026-11-24) the device records the balance itself, once an hour, and the History page shows it with a gap line.

This deliberately records what the live model said at the time (the calibration points and radiator ratings then in force) and is never recalculated: it is an exception to the history's measurements-only rule, and is documented as one.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Every 10 minutes of local clock time the device records heat meter, radiator and UFH energy (Wh) counted only while space heating (not hot water, not defrosting), plus the seconds counted and the seconds of heating where a figure was missing
- [ ] #2 Records go to /sdcard/balance/YYYY-MM-DD, one fixed-size record per bucket with reserved space and the bucket length in the header; buckets with no heating are still written
- [ ] #3 GET /api/history/balance?date= returns the day's buckets and their length, plus the bucket in progress for today, and storage unavailable with no card
- [ ] #4 The History page shows a chart of heat meter vs radiators + UFH (average W while heating) and a gap chart in W and percent, per 10 minutes or per hour, with periods of too little heating left as gaps
- [ ] #5 The page says the figures were recorded live with the calibration and ratings in force at the time
- [x] #6 firmware/CLAUDE.md documents the record and why it is an exception to measurements-only
- [x] #7 The accumulation rules have a desktop test; test_host/run.sh passes, the web app type-checks and the firmware builds with IDF v5.5.5
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved by the user 2026-11-24; full plan in the documentation link. Summary:
1. Pure-C accumulator (heat_balance_core.h): per sample, if space heating (heating, not DHW, not home_is_defrosting) and heat meter + radiator total + UFH total known -> add J and counted_s, else if heating -> unknown_s; local-hour rollover emits a finished record. New home_manager.has_total_radiator_output set in update_home.
2. heat_balance.{h,cpp}: fed from history_logger's on_sample_timer; finished hours queued under a mutex and appended by the history flush task to /sdcard/balance/YYYY-MM-DD (8-byte BALN header, 32-byte records with reserved space).
3. GET /api/history/balance?date= inside the /api/history/* wildcard in history_api.c; hours[] + current for today.
4. HeatBalance.tsx on the History page: hourly heat meter vs radiators+UFH (avg W while heating) and a gap chart (W left, % right); <10 min counted = gap; note that it was recorded live.
5. CLAUDE.md subsection; desktop test of the accumulator in run.sh.
6. Build with IDF v5.5.5; flash only with the user's OK.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-24 implementation: storage/heat_balance_core.h (hb_sample_t, hb_hour_t, packed 32-byte hb_record_t, hb_add, hb_record) with test_host/test_heat_balance.c (758 checks) in run.sh; storage/heat_balance.{h,cpp} (sample from on_sample_timer, up to 8 completed hours queued under a mutex, appended by the history flush task to /sdcard/balance/YYYY-MM-DD with an 8-byte BALN header, no duplicate hours; GET handler streams file hours + unflushed hours + current); heat_balance_api.h holds the handler prototype because history_api.c is C and home_manager.h is C++; history_api.c routes /api/history/balance. home_manager.has_total_radiator_output set in update_home (every radiator in a room has an output). UI: HeatBalance.tsx (keyed by date) on History: hourly avg W heat meter vs radiators+UFH, gap chart W/%, <10 min counted = gap, day summary line and the recorded-live note. CLAUDE.md subsection. run.sh green, tsc green, lint clean, IDF v5.5.5 reconfigure + build green with no new warnings. Not flashed yet.

2026-11-24: at the user's request, switched from hourly to 10-minute buckets (local :00, :10, ...). File version 2 stores the bucket seconds (600) in the header's former reserved u16; version 1 (hourly, 0 there) is still read as hourly and never appended to. API: bucketSeconds + buckets[] (was hours[]). UI: 10 min / Hourly toggle (hourly adds buckets by local clock hour); gap threshold 3 min per 10-min bucket, 10 min per hour. heat_balance_core.h renamed hour -> bucket (hb_bucket_t, start). Tests, tsc, lint, build green; flashed.
<!-- SECTION:NOTES:END -->
