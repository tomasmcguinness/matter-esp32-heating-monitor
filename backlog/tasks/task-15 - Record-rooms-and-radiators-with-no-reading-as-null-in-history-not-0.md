---
id: TASK-15
title: 'Record rooms and radiators with no reading as null in history, not 0'
status: Done
assignee: []
created_date: '2026-10-01 06:18'
updated_date: '2026-10-01 06:19'
labels:
  - firmware
  - bug
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Room history shows 0.00°C readings. The firmware uses 0 as "no reading" for room and radiator temperatures, but build_room_record/build_radiator_record only write the sentinel when no sensor is assigned, so an assigned sensor with no reading yet (after boot, null, implausible) is recorded as 0.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Room and radiator records write the sentinel when the temperature is 0
- [x] #2 Existing room/radiator files read 0 back as null
- [x] #3 Home records are unchanged
- [x] #4 Firmware builds
<!-- AC:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
build_room_record and build_radiator_record (history_logger.cpp) now write the sentinel when the temperature is 0, not only when no sensor is assigned. 0 is the managers' "no reading" value: before the first report, and for null/implausible reports since TASK-14. history_field_is_null (history_format.h) also reads 0 as null for KIND_ROOM and KIND_RADIATOR, so days already on the card chart as gaps; the home record is unchanged. firmware/CLAUDE.md notes the exception. Full idf build passes; not yet flashed or verified on hardware.

Not changed: endpoints still use 0 as a placeholder instead of a proper has-reading flag.
<!-- SECTION:FINAL_SUMMARY:END -->
