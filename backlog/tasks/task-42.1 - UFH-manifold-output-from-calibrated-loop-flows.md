---
id: TASK-42.1
title: UFH manifold output from calibrated loop flows
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-23 12:48'
updated_date: '2026-11-23 16:45'
labels:
  - ufh
  - firmware
  - web-ui
dependencies: []
references:
  - firmware/main/managers/calculations_manager.cpp
  - firmware/main/managers/radiator_manager.h
  - firmware/html_app/src/EditEmitter.tsx
parent_task_id: TASK-42
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A UFH manifold's output was the heat meter's residual after the radiators' modelled (dT50) output, so every radiator modelling error landed in the floor figure. Replace it with a direct figure: the user enters the manifold's loop count and each loop's flow (read off the manifold's flow gauges); the device records the heat meter's flow at that moment as a calibration point. Then manifold flow now = sum of loop flows x (heat meter flow now / heat meter flow at calibration), and manifold heat = that flow x water heat capacity x |manifold flow - return temperature|. Rooms keep their percentage shares of a manifold (loop-to-room assignment is out of scope). Decided with the user 2026-11-23: the residual method is removed outright (an uncalibrated manifold has no output), and a calibration may only be saved while the heat pump is space heating (not DHW) with a heat meter flow above 0.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A manifold's loop count (1-16) and each loop's flow in l/min can be set from its Edit page, and persist across a restart
- [x] #2 Saving loop flows records the heat meter's flow and the time at that moment as the calibration point
- [ ] #3 Saving is refused with a clear message unless the heat pump is space heating, not making hot water, and the heat meter reports a flow above 0
- [ ] #4 A calibrated manifold's current flow scales in proportion to the heat meter's flow (a 5% change in heat meter flow changes it 5%)
- [x] #5 A calibrated manifold's output is its current flow x water heat capacity x |flow - return| across its probes; 0 W while not space heating or making hot water or under 0.5 K across it
- [ ] #6 An uncalibrated manifold, or one missing a probe reading or the heat meter flow, has no output (absent, not 0); the residual method is gone
- [ ] #7 Rooms' UFH input from their percentage shares, and the home's UFH totals, follow the new manifold output
- [x] #8 The manifold's detail page shows the calibration and the live flow, dT and output
- [x] #9 MCP list_rooms, the MQTT config topic, code comments and firmware/CLAUDE.md describe the new method
- [x] #10 Firmware builds and test_host/run.sh passes
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved by the user 2026-11-23. Full plan: ~/.claude/plans/we-need-to-rework-magical-lerdorf-of-6b56e096-56bd-42cc-b4bd-666b0018bccb.md. Summary:
1. radiator_manager: radiator_t gains ufh_loop_count, ufh_loop_flow_dlpm[16] (0.1 l/min), ufh_ref_flow_lph, ufh_calibrated_at, transient has_ufh_flow/ufh_flow_lph. Persisted under new NVS key `ufh_loops` (radiators namespace, versioned), mirroring room_ufh; radiator_list blob untouched. Cleared when a manifold is deleted or changes type.
2. calculations_manager update_ufh_outputs(): per manifold, absent if uncalibrated / no heat meter flow / no heating state / probe missing; 0 if not space heating or DHW or |dT|<0.5 K; else flow = sum(loops) x hm_flow/ref_flow, W = flow_lph x dT x 4186/3600. Residual and rated-output split removed. Heat meter flow report also triggers update_ufh_rooms(false).
3. API: PUT /api/radiators/{id}/loops {loops:[l/min]} inside the existing /api/radiators/* PUT handler; 409 unless heating, not DHW, flow > 0; device stamps ref flow and time. GET manifold adds `ufh` object incl. canCalibrate.
4. UI: EditEmitter loops editor + Save calibration; Emitter detail shows calibration and live flow/dT/output; AddEmitter drops rated output for manifolds.
5. MCP list_rooms + tools json, MQTT config topic, comments, firmware/CLAUDE.md.
6. Build, test_host/run.sh; flash only with user's OK.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-23 implementation: radiator_manager (calibration fields, ufh_loops NVS blob written inside save_radiators_to_nvs, set_ufh_calibration, ufh_calibrated_flow_lph; type change away from manifold clears it); calculations_manager update_ufh_outputs rewritten per manifold (residual, radiator dependency and rated-output split removed; home has_ufh_output only when every manifold is known); app_main: heat meter FLOW report now calls update_ufh_rooms(false) (power report no longer does), PUT /api/radiators/:id/loops routed inside radiators_put_handler with 409 via ufh_calibration_blocker(), GET detail adds `ufh`, list adds ufhFlowLph/ufhCalibrated, calibration marks the MQTT config topic pending; MCP list_rooms manifolds now carry loop_flows_l_per_min + calibration_heat_meter_flow_lph (rated_output_w dropped for manifolds; test_server.c updated); MQTT config topic adds loop_flows_lpm + calibration_flow_lph, keeps rated_w for schema stability; UI: new UfhLoops.tsx on EditEmitter, Emitter shows calibration + flow + dT cards, Emitters list shows flow, AddEmitter drops rated output and opens Edit after adding a manifold. firmware/CLAUDE.md UFH section rewritten. Build green, test_host/run.sh green, tsc green, no new lint errors (repo has ~197 pre-existing). Not yet verified on the board: ACs 1-8 need a flash and a heating run. Not changed: MyHeatPump pusher still sends a manifold's rated_output_w (external contract).

2026-11-23 on the board (0.0.6-101-g00c3854-dirty): the user calibrated manifold 14 from the Edit page during a heating run: 3 loops x 1.8 l/min, ref 1113 l/h, calibratedAt stamped. Before that, while the heat pump's subscription was pending, the device reported canCalibrate false with the 'not space heating' message. The manifold probes (Quad Probe Sensor 0x274e) had read mirrored around 25 C since 2026-11-15 (divider built NTC-high); the user fixed the hardware ~16:30 UTC. After that: flow 37.32, return 30.61, current flow 324 l/h -> output 2527 W = 324 x 1.163 x 6.71, as expected. Still to verify on the board: AC1 persistence across a restart, AC3's 409 when not heating, AC4 proportional scaling (heat meter happened to be back at the ref flow), AC6 absent cases, AC7 room shares.
<!-- SECTION:NOTES:END -->
