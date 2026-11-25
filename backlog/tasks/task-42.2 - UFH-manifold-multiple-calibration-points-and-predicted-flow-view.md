---
id: TASK-42.2
title: 'UFH manifold: multiple calibration points and predicted flow view'
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-24 17:56'
updated_date: '2026-11-24 20:17'
labels:
  - ufh
  - firmware
  - web-ui
dependencies:
  - TASK-42.1
references:
  - firmware/main/managers/radiator_manager.cpp
  - firmware/main/managers/calculations_manager.cpp
  - firmware/html_app/src/UfhLoops.tsx
documentation:
  - >-
    ~/.claude/plans/we-need-to-rework-magical-lerdorf-of-6b56e096-56bd-42cc-b4bd-666b0018bccb.md
parent_task_id: TASK-42
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
TASK-42.1 gave a UFH manifold one calibration point (loop gauge readings + the heat meter's flow at that moment) and scales the manifold's flow in proportion to the heat meter's from it. On 2026-11-24, at a heat meter flow 14% above the calibration flow, radiators + UFH read 2.6-4.2% above the heat meter over two minutes of steady heating; one point cannot show whether the floor's share of the flow holds as the total changes. Allow several calibration points per manifold, taken at different heat meter flows, and show the predicted manifold flow they imply.

Decided with the user 2026-11-24: between points the manifold flow is interpolated linearly by heat meter flow; below the lowest or above the highest point it scales proportionally from that point (one point behaves exactly as TASK-42.1). A point with a different loop count from the saved points is refused until they are deleted. The user also asked for a view of the predicted flow rates the points give.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Up to 8 calibration points per manifold can be added from its Edit page during a heating run; a point within 3% of an existing point's heat meter flow replaces it
- [ ] #2 A point whose loop count differs from the saved points is refused with a message saying to delete them first; a 9th point is refused
- [ ] #3 A single point or all points can be deleted, and points persist across a restart
- [x] #4 The single calibration already saved by TASK-42.1 firmware loads as one point after the upgrade
- [ ] #5 The manifold's flow is interpolated linearly between the two nearest points by heat meter flow, and scaled proportionally from the end point outside them
- [ ] #6 The manifold's pages show the points with each one's share of the heat meter flow, and a chart of predicted manifold flow against heat meter flow marking the points, the current heat meter flow, and which parts are extrapolated
- [ ] #7 The chart's values come from the device's own calculation, so its value at the current flow equals the manifold's current flow
- [x] #8 MCP list_rooms, the MQTT config topic and firmware/CLAUDE.md describe the points
- [x] #9 Firmware builds with IDF v5.5.5, test_host/run.sh passes, and the web app type-checks
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved by the user 2026-11-24; full plan in the documentation link. Summary:
1. radiator_manager: ufh_point_t {ref_flow_lph, calibrated_at, loop_flow_dlpm[16]}; radiator_t keeps ufh_loop_count, gains ufh_point_count + heap ufh_points (manifolds only), sorted by ref flow. ufh_add_point (replaces within 3%, refuses loop-count mismatch / 9th), ufh_delete_point, ufh_delete_all_points, ufh_flow_at (pure interpolation). NVS ufh_loops v2; v1 loads as one point.
2. update_ufh_outputs uses ufh_flow_at.
3. API: PUT /api/radiators/:id/loops adds a point; DELETE /api/radiators/:id/loops[/index] inside the delete handler; ufh object gets loopCount, points[], predicted[] (sampled from ufh_flow_at).
4. UI: UfhLoops points table + add/delete; new UfhFlowCurve.tsx uPlot chart (points, current marker, dashed extrapolation) + table; Emitter.tsx summary.
5. MCP calibration_points, MQTT config calibration_points (+ existing two fields from highest-flow point), CLAUDE.md, host test for interpolation.
6. Build with IDF v5.5.5; flash only with the user's OK.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-24 implementation: new managers/ufh_curve.h (plain-C interpolation, ufh_curve_flow / ufh_curve_is_extrapolated) with test_host/test_ufh_curve.c (20 checks) in run.sh. radiator_manager: ufh_point_t, ufh_points heap array sorted by ref flow, ufh_add_point (3% replace, loop-count mismatch -> ESP_ERR_INVALID_STATE, 9th -> ESP_ERR_NO_MEM), ufh_delete_point, ufh_delete_all_points, ufh_flow_at, ufh_flow_is_extrapolated; NVS ufh_loops v2, v1 loaded as one point; points freed on remove/free/type change. update_ufh_outputs uses ufh_flow_at. app_main: PUT .../loops adds/replaces a point with 409 messages; DELETE .../loops[/index] routed at the top of radiators_delete_handler (it otherwise takes the last path segment as the radiator id, so .../14/loops/2 would have deleted radiator 2); ufh object now loopCount, maxPoints, points[], predicted[] (add_ufh_prediction, 50 l/h steps to 1.25x highest point or current flow, plus each point). MQTT config topic: calibration_points added, loop_flows_lpm/calibration_flow_lph from the highest-flow point. MCP list_rooms: calibration_points; tools json description updated; test_server fixture has two points. UI: UfhLoops (points table with share column, delete one/all, replace hint, placeholders from the latest point), new UfhFlowCurve (uPlot, solid/dashed, point dots, red line at current flow, table), Emitter.tsx summary + chart. firmware/CLAUDE.md updated. run.sh all green; tsc green; new files lint clean; IDF v5.5.5 build green (only pre-existing warnings). Not flashed yet.

2026-11-24: flashed 0.0.6-104-g1167548-dirty on IDF v5.5.5. TASK-42.1's v1 ufh_loops blob loaded as one point (1113 l/h, 3 x 1.8 l/min, 324 l/h); predicted curve has 30 entries, 0-1400 l/h at 50 l/h plus the point.

2026-11-24: a point added through the API landed during a defrost: the heat meter's flow had jumped from ~1,245 to 1,637 l/h with -6.8 kW, and heating_running stayed true, so ufh_calibration_blocker let it through. Deleted it (DELETE .../loops/1 worked as designed). Fixed: ufh_calibration_blocker refuses while defrosting or while the heat meter reads negative power; update_ufh_outputs gives 0 W in the same cases (|dT| had been counting the floor giving heat back as output). Also, at the user's request, the status header (HomeStatus.tsx, PageHeader.tsx) shows 'Defrosting' with a snowflake while home.defrosting is true. Flashed on IDF v5.5.5. Not yet seen during a real defrost.

2026-11-24 (user request): radiator output is now 0 W during a defrost too. home_is_defrosting() in home_manager.h (defrost flag or negative heat meter power) is shared by update_radiator_outputs, update_ufh_outputs and ufh_calibration_blocker. Recalculation on defrost start/end: update_heat_pump_running compares before/after; the heat meter power handler recalculates when its sign changes. New recalculate_all_radiator_outputs(). Browser history: isHeatingAt() in historyData.ts excludes defrost spans. CLAUDE.md updated. tsc and IDF v5.5.5 build green; not flashed yet.
<!-- SECTION:NOTES:END -->
