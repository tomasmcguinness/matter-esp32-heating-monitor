---
id: TASK-42
title: Model UFH manifolds as emitters and split their heat between rooms
status: In Progress
assignee: []
created_date: '2026-11-15 07:41'
updated_date: '2026-11-15 10:37'
labels:
  - firmware
  - web-app
  - ufh
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Supersedes TASK-39 (parked on feature/underfloor-heating). The Radiators tab becomes Emitters; a UFH manifold is an emitter added by hand (radiator record with type 100) with flow/return probes recorded for monitoring. Manifold output is the residual (heat meter minus modelled radiator output), split across active manifolds by rated output. Each room sets a % of each manifold's output, which feeds its heat input and measured heat loss. (A live Sankey on Home was built and then removed at the user's request.) Plan: ~/.claude/plans/how-can-we-bring-proud-rocket.md. Branch: feature/ufh-emitters.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A UFH manifold can be added, edited and deleted on the Emitters tab (formerly Radiators); its flow and return are shown and recorded in history, and existing radiator config loads unchanged
- [ ] #2 Each room can set a % of each manifold; shares persist in NVS key room_ufh; a total above 100% per manifold is rejected with 400 before any change
- [ ] #3 Manifold output is the heat-meter residual split by rated output across active manifolds (|flow-return| >= 0.5 K), 0 outside space heating, null while a room radiator lacks output
- [ ] #4 Room heatInput and measured heat loss include the room's UFH share; /api/home reports totalUfhOutput and ufhUnassignedOutput
- [x] #5 MCP list_rooms lists ufh_manifolds and per-room ufh_shares; host tests pass
- [ ] #6 Web app type-checks, firmware builds, and flashing over existing config keeps all rooms and radiators
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-15: Implemented on feature/ufh-emitters (uncommitted). Manifold = radiator record type 100; room shares in NVS key room_ufh; update_ufh_outputs() splits the residual by rated output across circulating manifolds; attribute_data_cb now matches probes on node+endpoint and walks every radiator; room POST/PUT validate ufhShares (400 before change); Emitters tab with Add Radiator / Add UFH Manifold; UFH share inputs on Add/Edit Room; HeatSankey on Home; MCP list_rooms ufh_manifolds + ufh_shares; firmware/CLAUDE.md documented. Verified: tsc clean, new files lint clean (changed files no worse than before), idf.py build OK (0x259850), host tests pass. NOT flashed or exercised on hardware: ACs 1-5 and 7 remain unverified on the board.

2026-11-15: Sankey removed from Home at the user's request (didn't work well enough); HeatSankey.tsx, its CSS and docs deleted.
<!-- SECTION:NOTES:END -->
