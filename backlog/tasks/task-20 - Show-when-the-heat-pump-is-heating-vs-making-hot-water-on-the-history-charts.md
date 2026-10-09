---
id: TASK-20
title: Show when the heat pump is heating vs making hot water on the history charts
status: In Progress
assignee: []
created_date: '2026-10-08 15:50'
updated_date: '2026-10-08 15:53'
labels:
  - firmware
  - web-app
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The history charts show flow, return, power and COP but not why they moved: a flow spike to 55 degC is a hot water run, not the heating misbehaving. The heat pump's running state is already subscribed for the Home page but never reaches history.

Record whether the picked heat pump is space heating or producing hot water, and show it as a thin horizontal bar along the bottom of every history chart.

Decisions agreed with the user:
- Layout: an 8px strip inside the plot area, pinned to its bottom edge just above the time axis, with a small key under the chart. Idle and not-recorded periods are blank.
- Signals: the Heat bit of Thermostat ThermostatRunningState on both sides. Heating = any Thermostat endpoint on the picked heat pump node has it set; hot water = any Water Heater endpoint has it set. WaterHeaterManagement HeatDemand is not used. When both are on the bar shows hot water.
- Scope: every chart - History (Power, Temperatures, COP), Room History, and the today charts on the Room and Radiator pages.
- Days recorded before this change must stay readable and simply show no bar.

Out of scope: Home Assistant discovery entities for the two states.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The home history records a heating state and a hot water state for each slot, null when the heat pump has not reported or is not picked
- [ ] #2 History files written before this change still load, and their charts show no bar and no error
- [ ] #3 History page Power, Temperatures and COP charts show a bar along the bottom of the plot coloured for heating and hot water, blank when idle
- [ ] #4 Room History charts and the today charts on the Room and Radiator pages show the same bar at the same times
- [ ] #5 When heating and hot water are both reported at once the bar shows hot water
- [ ] #6 A key naming the two colours is shown under a chart that has a bar, and is absent when there is nothing to show
- [ ] #7 When the heat pump's subscription is lost the recorded state becomes null rather than holding the last value
- [x] #8 firmware/CLAUDE.md describes the new history column and that the heat pump running state now reaches history
- [ ] #9 Firmware builds and the web app passes lint
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented 2026-10-08. Firmware: rec_home_t reserved0 became heating_running (record size unchanged); update_heat_pump_running() in app_main.cpp sets home_manager heating_running / dhw_running from the Heat bit of ThermostatRunningState and is called after each heat pump report, when the node's readings are forgotten, and when the pick changes; history_logger writes the column and history_api names it heatingRunning. Web app: modeSpans() in historyData.ts, useHeatPumpModes.ts hook, Chart `modes` prop drawn by a uPlot draw hook plus a key, wired into History, RoomHistory, RoomTodayChart, RadiatorTodayChart and RadiatorChart.

Verified: idf.py build succeeds (links and produces heating-monitor.bin), tsc -b passes, eslint is clean on every file touched. NOT verified: nothing has been flashed, so criteria 1-7 are untested on the device and no chart has been seen with a real bar. Criterion 9 left unchecked because `npm run lint` across the whole app still reports roughly 190 errors in files this task did not touch (no-var, prefer-const, no-explicit-any).

Known limit: history_api buckets are integer means, so a 0/1 column reads 1 only when the mode held for the whole bucket. At 800 points on a 5 s day a run is drawn up to about 2 minutes short at each end.
<!-- SECTION:NOTES:END -->
