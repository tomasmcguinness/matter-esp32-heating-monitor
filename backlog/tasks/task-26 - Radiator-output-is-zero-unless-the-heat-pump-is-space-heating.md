---
id: TASK-26
title: Radiator output is zero unless the heat pump is space heating
status: In Progress
assignee: []
created_date: '2026-10-12 17:52'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A radiator's output is derived from temperatures alone, so one that is still warm after the heat pump stops, or during a hot water run, shows output it is not delivering. Gate the figure on home_manager's heating_running: not heating, or heating state unknown, is 0 W. Applies to the live figure in the firmware and to the browser-derived output line on the radiator and room history charts. A radiator with no flow or return reading still shows no output.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A radiator with readings reports 0 W while the heat pump is idle, making hot water, or its heating state is unknown
- [ ] #2 A radiator without a flow or return reading still reports no output
- [ ] #3 Outputs are recalculated and pushed over the radiator WebSocket channel when the heating state changes
- [ ] #4 A room whose radiators total 0 W reports no measured heat loss, not 0 W/degC
- [ ] #5 The output line on radiator history charts is zero outside heating spans
- [ ] #6 idf.py build succeeds
<!-- AC:END -->
