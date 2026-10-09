---
id: TASK-19
title: Null a device's readings when its subscription is lost
status: In Progress
assignee: []
created_date: '2026-10-08 12:17'
updated_date: '2026-10-08 12:21'
labels:
  - firmware
  - web
  - bug
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
When a Matter node's subscription ends or fails to establish, its readings keep their last value, so history, MQTT and the web UI go on reporting it as current. Only the outdoor temperature and heat pump readings are dropped today. Readings should become genuinely nullable (a has-reading flag, not a magic 0) at the node endpoint and through rooms, radiators, the home's heat and electrical meters, history, MQTT, the REST API, WebSocket pushes and the web app. Heat loss must not be calculated from missing readings. The priming report of the next subscription restores the values.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A lost or failed subscription clears every reading that node supplied: endpoint values, room temperature, radiator flow/return, heat meter and electrical meter readings
- [x] #2 Rooms and radiators no longer use 0 as 'no reading'; a real 0.00 °C or sub-zero reading is kept
- [x] #3 History records the sentinel for a missing room/radiator reading and reads a stored 0 back as a reading, not null
- [ ] #4 MQTT, REST and WebSocket payloads carry null for missing readings and the web app dashes them
- [x] #5 Room heat loss and radiator output are not calculated without valid readings and are reported as absent
- [x] #6 The companion API payload is unchanged
- [x] #7 firmware/CLAUDE.md describes the new behaviour
- [ ] #8 Firmware builds and the web app lints
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented and built (idf.py build passes, tsc --noEmit clean). Not flashed: AC #1 and #4 are implemented but unverified on hardware. AC #8: firmware builds; `npm run lint` fails with 192 problems that predate this change (no-explicit-any, no-var, prefer-const across the app), so the lint half is not met. Left In Progress pending a hardware check.

Changes: endpoint_entry_t::has_measured_value with set/get/clear in node_manager; has_* flags on room_t and radiator_t; forget_readings_if_unsubscribed() in app_main.cpp called from the terminated and subscribe-failed callbacks; calculations_manager no longer computes radiator output or room heat loss from missing readings (also guards the divide by a zero indoor-outdoor delta); history snapshot uses the flags and history_field_is_null() no longer reads 0 as null (pre-2026-10-01 room/radiator files chart those slots as 0.00 degC again); REST/WebSocket JSON emits null; Radiators.tsx, Rooms.tsx, Radiator.tsx, Layout.tsx handle null.
<!-- SECTION:NOTES:END -->
