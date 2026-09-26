---
id: TASK-12
title: Show a radiator's current status and today's readings on the Radiator page
status: In Progress
assignee: []
created_date: '2026-09-26 05:40'
updated_date: '2026-09-26 05:43'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The /radiators/:id page is a placeholder. Show the radiator's name as the heading, its current status (flow, return, mean water temp, output, room temperature), and today's recorded flow/return with the room's temperature and derived output.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 GET /api/radiators/:id reports meanWaterTemperature, roomId, roomName and roomTemperature (null room fields when unassigned)
- [ ] #2 Radiator page heading shows the radiator's name
- [ ] #3 Radiator page shows status cards for flow, return, mean water temperature, output and room temperature
- [ ] #4 Beneath the status, a chart shows today's flow, return and room temperature, with derived output on the right axis
- [ ] #5 Room history page radiator charts are unchanged
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Firmware: radiator_get_handler now adds meanWaterTemperature, plus roomId/roomName/roomTemperature. The room is found by walking room->radiators[], and the three fields are null when the radiator isn't in a room.

Web:
- radiator history helpers (RADIATOR_COL, ROOM_COL, radiatorOutputW, loadRadiatorSeries, roomTempByTs, types) moved to historyData.ts
- RadiatorChart moved to RadiatorChart.tsx, with optional showRoomTemp/title props
- RoomHistory uses both; behaviour unchanged
- new RadiatorTodayChart.tsx: dateless, 200 points, loads the radiator then the room one after the other
- Radiator.tsx rewritten: name heading, status cards (flow, return, MWT, output, room temperature linked to the room), today's chart; typed and lint-clean

Verified: eslint clean on the changed files; app_main.cpp compiles on its own. The full idf build is blocked by an in-progress edit to Radiators.tsx (unused NavLink import, TS6133), which is not part of this task. Not yet verified on hardware.

Flagged, not fixed: radiator_get_handler and room_get_handler dereference a missing radiator/room (crash on an unknown id).
<!-- SECTION:NOTES:END -->
