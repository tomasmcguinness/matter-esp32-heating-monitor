---
id: TASK-39
title: Model the underfloor heating circuit and split its heat between rooms
status: In Progress
assignee: []
created_date: '2026-11-08 14:07'
updated_date: '2026-11-08 14:16'
labels:
  - firmware
  - web-app
  - ufh
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The house has one underfloor heating circuit that the monitor does not model, so metered heat runs well above modelled radiator output (8 Nov: 3.5 kW metered, 1.8 kW modelled). A quad probe sensor (node 10062) now measures UFH flow (probe 1) and UFH return (probe 2).

Decided with the user:
- Exactly one circuit, configured on the Home.
- Its output is the residual: heat meter output minus the sum of modelled radiator outputs. It is absent while any configured radiator lacks a reading.
- A fixed % per room splits that output, and each room's share feeds its measured heat loss.
- The probes are shown and recorded, and gate the output (|flow − return| ≥ 0.5 K).
- The work is on branch feature/underfloor-heating.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Edit Home picks UFH flow and return sensors and a % share per room (total at most 100), saved in NVS so older blobs still load
- [ ] #2 GET /api/home returns the UFH config, flow, return and ufhOutput; ufhOutput is the heat meter minus radiator output while heating, 0 for hot water or a closed gate, and null when a configured radiator has no reading
- [ ] #3 A room with a share adds pct x ufhOutput to its heat input for measured heat loss, and its JSON shows ufhShare and ufhInput
- [ ] #4 UFH flow and return are recorded in the home history (the last two reserved fields), charted on History, and listed as MCP series
- [ ] #5 Home shows UFH output, flow, return, ΔT and an implied flow rate; Room shows its UFH share
- [ ] #6 Builds, lints, and flashes over an existing configuration without losing any of it
- [x] #7 firmware/CLAUDE.md documents the UFH model
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-08: Implemented on feature/underfloor-heating (uncommitted). Firmware: home_manager config + NVS tail and share helpers; update_ufh_output() in calculations_manager; room heat input = radiators + share; app_main probe dispatch, update_ufh_rooms() (30 s throttle on heat meter power, forced on probe report or heating/DHW change), subscription-loss clearing, PUT validation (400 before any change), home/room JSON, room removal drops share; rec_home_t reserved1/2 -> ufh_flow/return_temp_c100, history_api column names, MCP series. Web: Edit Home UFH section with per-room %, Home UFH cards + implied flow, Room share card, History chart columns.

Build: links (heating-monitor.bin 0x256200). Web app: tsc clean; lint problem count per changed file equal to main (the repo has ~210 pre-existing lint errors).

NOT flashed: /dev/ttyACM0 is the Energy Manager (ESP32-P4); esptool refused before writing. The heating monitor has no OTA route, so it needs connecting by USB. ACs 1-6 remain unverified on hardware.

Expect ufhOutput to be null until the Sitting Room radiator has sensors again: it is in a room with no output, which by design makes the residual absent.
<!-- SECTION:NOTES:END -->
