---
id: TASK-44
title: Push the current heating status to the MyHeatPump server over HTTP
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-18 17:29'
updated_date: '2026-11-18 17:34'
labels:
  - firmware
  - myheatpump
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Every N seconds (default 60) POST the latest readings as JSON to <server>/api/status, with X-Api-Key when a key is set. Payload: timestamp, system readings, rooms with radiators and UFH shares, UFH manifolds. Nulls for missing readings. Configured like emoncms (NVS, /api/... GET/PUT, Settings page). Own task, 5 s timeout, no retries or queueing.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 URL, API key, interval and enabled flag are saved in NVS and edited from the Settings page; disabled by default and when no URL is set
- [x] #2 Posts run on their own task with a 5 s HTTP timeout and never block sensor reading or the MCP server
- [x] #3 A failed post is logged and the next attempt is the next interval; old payloads are not queued
- [x] #4 The JSON is streamed with a bounded buffer rather than built whole in memory
- [x] #5 Room and radiator configuration comes from mcp_directory and readings from the history logger snapshot, with no new sensor-reading code
- [x] #6 The firmware and web app build
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
New module main/myheatpump/myheatpump_pusher.{h,cpp}, modelled on the emoncms uploader but with no buffer and no backoff. Added history_logger_room_reading() and history_logger_radiator_reading() so other tasks can read the snapshot. mcp_directory is called on the HTTP task through httpd_queue_work, waiting up to 2 s, and the previous copy is used if the HTTP task is busy. The body is written in two passes (count, then send in 256-byte chunks) from a snapshot in PSRAM. Added GET/PUT /api/myheatpump/config, GET /api/myheatpump/status and MyHeatPumpSettings.tsx. Raised max_uri_handlers from 44 to 48 because 43 are now registered. Verified that idf.py build is clean with no warnings in the touched files, tsc --noEmit passes, and on the host the payload writer gives valid JSON whose counted length equals the bytes sent. Not yet checked on hardware against a live server.
<!-- SECTION:NOTES:END -->
