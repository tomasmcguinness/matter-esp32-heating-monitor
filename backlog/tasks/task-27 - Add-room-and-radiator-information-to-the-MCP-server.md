---
id: TASK-27
title: Add room and radiator information to the MCP server
status: In Progress
assignee: []
created_date: '2026-10-13 19:16'
updated_date: '2026-10-13 19:18'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The MCP server only exposes the home series. Add a list_rooms tool giving each room (name, target temperature, predicted heat loss) with its radiators (name, rated output at dT 50) and the series ids recorded for each, and let get_series return room temperature and radiator flow/return temperature from the per-room and per-radiator day files. Radiator heat output stays derived off the device: the tool description gives the formula. Still read-only.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 list_rooms returns rooms with their radiators, names, ratings, series ids and recorded date range
- [ ] #2 get_series accepts room_<id>_temp_c, radiator_<id>_flow_temp_c and radiator_<id>_return_temp_c, alone or mixed with home series
- [x] #3 A call's room and radiator reading is capped so it cannot hold the web server for much longer than a 31 day home read
- [x] #4 test_host/run.sh passes with tests for the new tool and series
- [x] #5 idf.py build succeeds
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented on branch mcp-rooms-radiators (off radiator-output-when-heating). test_host/run.sh passes (129 + 146 checks) and idf.py build passes. Not yet run on the board: criteria 1 and 2 are covered by the host tests against fixture files but still need a call against the real card and real room names.

list_rooms reports configuration only, no live readings. Radiator heat output is not a series; the tool description gives the formula. The cap is 124 room-or-radiator days per call.
<!-- SECTION:NOTES:END -->
