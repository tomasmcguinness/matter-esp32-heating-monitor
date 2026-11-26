---
id: TASK-47
title: Add a Heat tab showing where the metered heat goes as a Sankey diagram
status: In Progress
assignee: []
created_date: '2026-11-26 07:16'
updated_date: '2026-11-26 07:35'
labels:
  - web-ui
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A live Heat tab: the heat meter's reading on the left, flowing to Radiators and UFH, then to each room on the right. Web-app only; the data is already on GET /api/home and GET /api/rooms.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 A Heat tab at /heat draws heat meter -> Radiators / UFH -> rooms as a Sankey
- [x] #2 Heat the model does not account for shows as Unaccounted; a model above the meter shows as Model over meter
- [x] #3 Not heating, no meter reading and unknown UFH each show a clear state instead of a misleading diagram
- [ ] #4 Radiator outputs update live from the radiator channel; UFH shares refresh every 30 s
- [x] #5 No new npm dependency; build still emits only index.html, app.css and app.js
- [x] #6 The Heat tab shows the current total heat loss (predicted and measured) at the current outdoor temperature, beside the heat meter
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented in html_app/src/Heat.tsx, sankey.ts and Heat.css; route and nav are in App.tsx; documented in firmware/CLAUDE.md. Web-app only, with no firmware change. The new files lint clean, and tsc and vite build pass (still only 3 output files, +~2 KB js). The layout maths was checked with sample data in node. It has NOT been checked in a browser or on the board, so live radiator updates and the 30 s refresh (AC #4) are untested.

2026-11-26: Added HeatLoss.tsx cards (predicted and measured heat loss at the current outdoor temperature, plus the heat meter), using the existing predictedHeatLossAtCurrentTemperature and measuredHeatLossAtCurrentTemperature. Built with IDF v5.5.4 and flashed. build/bootloader had a CMake cache from IDF v5.5.5 (2026-11-23), which failed the configure step; I deleted that generated directory and it rebuilt on v5.5.4. The board served the new app.js after boot. At 204 s of uptime: outdoor 2.7 °C, predicted 1409 W from 4/11 rooms, measured 358 W from 1 room, with subscriptions still coming back. Not yet viewed in a browser.
<!-- SECTION:NOTES:END -->
