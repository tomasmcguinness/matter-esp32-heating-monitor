---
id: TASK-30
title: Configurable timezone and NTP server
status: In Progress
assignee:
  - '@claude'
created_date: '2026-10-29 15:46'
updated_date: '2026-10-29 15:53'
labels:
  - firmware
  - web-ui
  - mcp
  - release
dependencies: []
priority: high
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The firmware hardcodes UK time (POSIX TZ GMT0BST,M3.5.0/1,M10.5.0 in firmware/main/storage/time_sync.c) and the NTP server pool.ntp.org. History day files, daily totals and the subscription log are named and bucketed by local day, so anyone outside the UK gets wrong day boundaries; the MCP server only accepts Europe/London or UTC and its tool schemas name Europe/London. For a public release the zone and NTP server must be user settings (web UI, stored in NVS), defaulting to today's values so existing installs are unchanged. The device has no tz database, so zones come from a curated IANA-name -> POSIX-TZ table. Changes apply on restart (like the MQTT settings) to avoid changing TZ while the logger and other tasks compute local midnight.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 With nothing saved, the device uses Europe/London and pool.ntp.org exactly as before
- [ ] #2 Settings page lets the user pick a timezone from a list and enter an NTP server, saved across reboots
- [ ] #3 Invalid input (unknown zone, empty or whitespace-containing NTP server) is rejected with a 400 and a message
- [ ] #4 After a restart the chosen zone determines history day boundaries and the NTP server used
- [x] #5 MCP tools accept and report the configured zone (plus UTC); tools/list names the configured zone, not Europe/London
- [x] #6 MCP range queries still find samples near midnight in day files written before a zone change
- [x] #7 Host tests cover the zone table and the MCP server with a non-UK zone, and test_host/run.sh passes
- [x] #8 firmware/README.md documents the time settings
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved plan (2026-10-29):
1. New main/storage/timezones.{h,c} (plain C): static {iana, posix} table (~40 zones), timezones_posix_for(), timezones_count()/timezones_at().
2. New main/storage/time_settings.{h,cpp} modelled on mqtt/mqtt_settings.cpp: NVS namespace "time", keys zone/ntp; defaults Europe/London / pool.ntp.org; save validates.
3. time_sync.c: load settings at start, setenv TZ from table, NTP server from a static buffer; add time_sync_zone_name(). Takes effect on restart.
4. app_main.cpp: GET/PUT /api/time/config next to /api/mqtt/config.
5. html_app/src/TimeSettings.tsx modelled on MqttSettings.tsx, mounted from Settings.tsx.
6. MCP: mcp_server_register(server, local_zone, version); zone_arg accepts configured zone + UTC; mcp_tools.json uses {{LOCAL_ZONE}} placeholder substituted in tools/list; day iteration starts one day earlier to cover files written before a zone change.
7. Host tests: timezone table, non-UK zone in test_server, updated error text.
8. README section.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-29: Implemented. New storage/timezones.{h,c} (55 IANA->POSIX entries, alphabetic abbreviations only so newlib needn't parse <+03>), storage/time_settings.{h,c} (NVS ns "time", keys zone/ntp), time_sync.c loads both once per boot (lazily, since start_webserver/MCP registration runs before time_sync_start), GET/PUT /api/time/config, TimeSettings.tsx on the Settings page.

Found during implementation (kept in scope, needed for AC #4/#6): after a zone change the logger can reopen a same-named day file whose header base_ts is the old zone's midnight; write_series_day() now places samples by timestamp in that case instead of at a slot counted from the new midnight. MCP scan_series() opens one day file beyond each end of the range and never emits a timestamp twice -- a west-moving zone (London->New York) otherwise misses samples; host test test_other_zone fails without it.

Verified: test_host/run.sh passes (1854 + 167 checks); idf.py build links with new sources and the React app rebuilt. NOT yet verified on hardware: ACs #1-#4 need a flash and a restart-with-a-new-zone check (plan's Verification steps 3-4).
<!-- SECTION:NOTES:END -->
