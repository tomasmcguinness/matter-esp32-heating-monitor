---
id: TASK-41
title: 'Account for the time, electricity and heat that defrosting uses'
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-09 07:19'
updated_date: '2026-11-14 07:41'
labels:
  - firmware
  - heat-pump
  - mcp
  - history
dependencies:
  - TASK-40
references:
  - firmware/main/mcp/mcp_cycles.c
  - firmware/main/mcp/mcp_series.c
  - firmware/main/mcp/mcp_tools.json
  - firmware/CLAUDE.md
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The user wants to know how much time and electricity the heat pump spends defrosting, and how much that costs in efficiency. Defrosting runs the cycle in reverse to de-ice the outdoor coil: it draws electricity while taking heat back out of the water, which shows up as negative heat-meter power.

Depends on TASK-40, which records a per-sample defrost flag in the home history. The flag is independent of the mode: a defrost happens inside heating runs and inside hot water runs, and must be accounted to whichever it happened in.

The main consumer is the read-only MCP server (firmware/main/mcp/, TASK-22): summarise_cycles and compare_periods. Runs are split on mode change, so defrost must NOT split a run or count as a compressor start. Defrost energy stays inside the run's and the period's COP, because that is the real cost of running in icing conditions; the defrost figures make it visible rather than removing it. The implausible-heat filter (above 3x rated output, or instantaneous COP above 10) must not treat legitimately negative defrost heat as an artefact, and must not let it in silently either: decide and document how negative heat during a defrost is handled.

Periods or days without the defrost flag report the defrost figures as unknown (null), not zero.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Each run in summarise_cycles reports the number of defrosts, minutes spent defrosting, electricity used while defrosting (kWh) and net heat during defrost (kWh, normally negative)
- [ ] #2 The period summary reports the same totals overall and split by heating and hot water, plus defrosts per hour of compressor running
- [ ] #3 A defrost never splits a run, changes its mode or counts as a compressor start
- [ ] #4 Run and period COP still include defrost energy, and this is stated in the tool description
- [ ] #5 Runs and periods with no recorded defrost flag report the defrost figures as null, not zero
- [ ] #6 compare_periods carries the defrost totals for both periods
- [ ] #7 list_series and the tool descriptions in mcp_tools.json describe the defrost series and figures, and no longer say defrost is not recorded
- [ ] #8 Host tests (test_host/run.sh) cover a synthetic night with defrosts during both a heating run and a hot water run, and a day without the flag
- [ ] #9 firmware/CLAUDE.md MCP section documents the defrost accounting, including how negative heat during defrost is treated by the implausible-heat filter
- [ ] #10 The History page's "Electricity in" card shows the day's electricity used while defrosting in brackets in the same box, e.g. "6.6 kWh (0.4 kWh defrost)"; days without the defrost flag show no brackets
- [ ] #11 GET /api/history (home) reports the day's defrost energy as "defrost":{"heatWh","electricalWh","minutes"}, or "defrost":null when no slot of the day recorded the flag
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
First slice (user request 2026-11-14): defrost electricity on the History page.

1. history_totals.h/.c: add history_defrost_t {known_slots, defrost_slots, energy_pair_t energy} and history_defrost_add(d, rec, interval_s). Kept out of history_totals_t, whose layout is the 64-byte cop-totals cache record, so the cache does not change. Reusable for the MCP work later.
2. history_api.c stream_file: accumulate history_defrost_t for home days; tail gains "defrost":{"heatWh","electricalWh","minutes"} or null when no slot carried the flag (old/converted days). Electricity is included in energyWh.electrical, not in addition to it.
3. html_app: History.tsx EnergyCard takes an optional note; Electricity in shows "(x.x kWh defrost)" as small muted text when defrost is not null.
4. Host tests: test_server checks the defrost object on a v3 day with defrost slots and null on a day without the flag.
5. CLAUDE.md: document the field.

The MCP ACs (#1-#9) follow in a later slice.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Slice 1 (History page defrost electricity), 2026-11-14:
- history_totals.h/.c: history_defrost_t {known_slots, defrost_slots, seconds, energy} + history_defrost_add(). Not in history_totals_t, which is the fixed 64-byte cop-totals cache record, so the cache and /api/history/totals are unchanged (no defrost there).
- history_api.c stream_file: home tail gains "defrost":{heatWh, electricalWh, minutes}, or null when known_slots == 0. tail buffer 448 -> 512.
- html_app: historyData.ts DefrostEnergy type; History.tsx EnergyCard takes an optional note, shown as <small class="text-muted">(x.x kWh defrost)</small> after the figure. No brackets when defrost is null.
- test_host/test_totals.c (23 checks), wired into run.sh; mutation-checked (forcing every slot to count as defrost: 8 failures; known only when flag set: 3 failures). All host tests pass: 63, 23, 1854, 169.
- Firmware and web app build. CLAUDE.md documents the field, the test, and that an Interview (not re-pairing) picks up the defrost endpoint.
- AC #10/#11 awaiting a flash to verify on the board.
<!-- SECTION:NOTES:END -->
