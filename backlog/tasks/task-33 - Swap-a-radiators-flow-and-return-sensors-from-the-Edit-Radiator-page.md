---
id: TASK-33
title: Swap a radiator's flow and return sensors from the Edit Radiator page
status: In Progress
assignee: []
created_date: '2026-10-31 07:59'
updated_date: '2026-10-31 08:00'
labels:
  - web-app
dependencies: []
priority: low
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A radiator's flow and return probes can be assigned the wrong way round, as on the Back Bedroom Radiator, and correcting that meant re-picking both sensors from long dropdowns. The Edit Radiator page should show the two sensor dropdowns side by side with a swap button between them.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The flow and return sensor dropdowns sit side by side on wide screens and stack at phone width
- [ ] #2 A swap button between them exchanges the two selections
- [ ] #3 A swap is only stored when Save is pressed; Cancel discards it
- [x] #4 tsc passes and the firmware builds
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Web app only: add a swap handler to EditRadiator.tsx that exchanges the flowSensor and returnSensor state, and lay the two SensorSelects out in a Bootstrap row with the swap button between them. The existing PUT /api/radiators/:id already carries all four sensor ids, so no firmware code changes. The Add Radiator page is unchanged.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented in firmware/html_app/src/EditRadiator.tsx. tsc -b passes and idf.py build produces heating-monitor.bin with the button in the embedded app.js. eslint reports 12 problems on the file, all on lines this change did not touch (prefer-const, no-var, no-explicit-any and similar).

NOT verified: nothing flashed, so AC 1-3 are untested in a browser. Left In Progress pending that check. Days already recorded keep their swapped flow and return, because the card stores them as logged.
<!-- SECTION:NOTES:END -->
