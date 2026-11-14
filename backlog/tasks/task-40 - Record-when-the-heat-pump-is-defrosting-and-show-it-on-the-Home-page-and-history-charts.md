---
id: TASK-40
title: >-
  Record when the heat pump is defrosting and show it on the Home page and
  history charts
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-09 07:18'
updated_date: '2026-11-12 19:46'
labels:
  - firmware
  - heat-pump
  - history
dependencies: []
references:
  - firmware/main/storage/history_format.h
  - firmware/main/managers/heat_pump_manager.h
  - >-
    ~/development/matter-esp32-modbus-adapter/backlog/tasks/task-4.1 -
    KronotermAdapt-report-defrost-as-a-Boolean-State-endpoint.md
  - >-
    https://github.com/kosl/kronoterm2mqtt/blob/main/kronoterm2mqtt/definitions/kronoterm_ksm.toml
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The user wants to see when the heat pump defrosts and, later, how much time and electricity defrosting uses (see TASK-41, defrost accounting). Today the monitor records only two mode flags, heating_running and dhw_running (TASK-20); firmware/CLAUDE.md and the MCP tool descriptions state there is no defrost state.

Source: the Kronoterm ADAPT Modbus adapter (separate repo ~/development/matter-esp32-modbus-adapter, its TASK-4.1) is gaining a Boolean State sub-part of the heat pump, identified by a Descriptor TagList label, whose StateValue is true while the heat pump defrosts (Kronoterm register 2011). Read the label and endpoint layout from that example's README once TASK-4.1 is done. Other heat pumps without such an endpoint must keep working, with defrost simply unknown.

Defrost is orthogonal to the mode: it happens during heating and during hot water, so it is a third independent flag alongside heating_running / dhw_running, never a third value of the mode. A defrost inside a heating or DHW run must not change that run's mode.

Storage: rec_home_t (firmware/main/storage/history_format.h) is fixed at 28 bytes with no spare fields left, and every reader (history_api.c, history_totals.c, mcp_server.c) rejects a file whose version or record_size differs. Widen the home record (new format version) to add the defrost field plus fresh spare fields for future quantities, rather than squeezing the flag into an existing field. Append only: never reorder existing fields (history_api.c integrates fields 0 and 1; History.tsx charts by position). Room and radiator records do not change, but HISTORY_VERSION is shared by all kinds and every reader checks it on room and radiator files too, so a plain bump would make all of those unreadable: either version the home kind separately or have the converter rewrite their headers.

Existing history is converted OFFLINE, not by the firmware: the user powers the board off, copies the SD card to a PC, runs a host-side converter over a copy, checks it, copies it back, then flashes and boots the new firmware. The firmware contains no migration code and only reads and writes the new format. The converter is a small C program alongside test_host, built from the same history_format.h so it cannot drift from the firmware's layout. Converted days carry the null sentinel for defrost (unknown, not false).

Getting the order wrong is not destructive today: write_series_day() (history_logger.cpp) skips a day file whose header does not match, and the readers reject one, so the worst case is missing data. That must stay true. Because a missed conversion would otherwise show up only as a "Bad header" log line, the new firmware must make an old-format history file obvious to the user.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 When the selected heat pump exposes the defrost endpoint, the monitor subscribes to it and keeps its current state
- [ ] #2 The Home page heat pump section shows when the heat pump is defrosting
- [ ] #3 The defrost state is recorded in history at the normal sampling interval
- [x] #4 The home record is widened under a new format version with the defrost field and new spare fields appended after the existing ones, which keep their order
- [x] #5 A host-side converter turns a copied SD card's history into the new format, leaving every existing value unchanged and defrost unknown, and reports what it converted
- [x] #6 The converter never modifies its input in place, and running it on already-converted files changes nothing
- [x] #7 The converter verifies its output against its input and reports any day it could not convert, leaving that day untouched
- [x] #8 Converted days chart, total and summarise exactly as they did before conversion, and room and radiator history from before the upgrade still reads
- [ ] #9 The firmware contains no migration code; given an unconverted file it never overwrites or truncates it, and the user can see that history needs converting without reading the serial log
- [ ] #10 A heat pump with no defrost endpoint behaves as before, with defrost unknown
- [ ] #11 The History charts mark defrost periods, distinguishable from the heating and hot water bands, including a defrost during a hot water run
- [x] #12 Host tests (test_host/run.sh) cover converting an old-format day (values preserved, defrost null), re-running the converter on converted output, and reading back the defrost flag
- [x] #13 firmware/CLAUDE.md and the history_format.h comments document the new record layout, and the README or firmware/CLAUDE.md gives the step-by-step card conversion procedure, including taking a full backup of the card first
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Decided with the user (2026-11-12): widen the record and convert offline as the description says, even though main still has reserved1/reserved2 (UFH, which took them, is parked on its own branch). Defrost endpoint label: "Defrost". Branch: feature/defrost, from main.

1. Format (history_format.h)
   - rec_home_t grows from 28 to 40 bytes: the 14 existing fields unchanged (reserved1/reserved2 stay spare), then `defrosting` (field 14, int16 0/1) and reserved3..reserved7.
   - Versions are per kind: history_version(KIND_HOME) = 3, rooms and radiators stay at 2, so their files are untouched. HISTORY_VERSION is removed so every reader has to be revisited.
   - HISTORY_HOME_V2_VERSION / HISTORY_HOME_V2_RECORD_SIZE describe the old layout, with a static_assert that it is the new record's 28-byte prefix.
   - history_unsigned_mask() widens to uint32_t.
   - history_check_header() returns ok, old-format or bad, and every reader and writer uses it.
2. Readers
   - history_api.c, history_totals.c and mcp_server.c use history_check_header().
   - history_api names the new columns. An old-format day answers 200 with storage "needsConversion" instead of a 500.
   - READ_CHUNK_RECORDS shrinks so s_read_buf stays about 7 KB, and the head and point buffers grow.
   - Totals backfill does not cache an old-format day as zeros: it stops there until the file is converted.
3. Writer (history_logger.cpp)
   - build_record() fills defrosting.
   - write_series_day() already skips a bad header without writing. It now counts an old-format file.
   - On its first run the flush task scans /sdcard for old-format home-* files. history_logger_old_format_days() reports how many it found.
4. Defrost source
   - Subscribe heat pump nodes to BooleanState::StateValue on a wildcard endpoint.
   - Store it per endpoint in heat_pump_manager (has_state_value / state_value).
   - update_heat_pump_running() sets home_manager has_defrosting / defrosting from the picked heat pump node's endpoint named "Defrost" (case-insensitive; the name comes from the TagList label). No such endpoint means absent.
   - Readings are forgotten with the subscription, like the other flags.
5. API and UI
   - GET /api/home and the home channel gain `defrosting` (true/false/null) and `historyNeedsConversion` (a count).
   - heatPump gains `defrosting` only when the node has a Defrost endpoint, and HeatPump.tsx shows a Defrost card then.
   - PageHeader shows a warning on every tab while historyNeedsConversion > 0, and History.tsx explains a needsConversion day.
   - modeSpans() also builds defrost spans, which HistoryChart draws as a second, thinner row under the mode strip with its own key entry, so a defrost inside a hot water run stays visible.
   - History.tsx FIELDS is updated.
6. Converter: firmware/history_convert/history_convert.c plus build.sh, built from history_format.h.
   - `history_convert IN OUT` converts IN/home-* into OUT. It refuses if OUT is IN, and never writes to IN.
   - A v2 file has each record's 28 bytes copied and the new fields set to the sentinel. A v3 file is copied byte for byte. Anything else is reported and not written.
   - Each file goes to a temp file, is re-read and checked against the input, then renamed.
   - A partial trailing record is dropped and reported; readers already ignore it.
   - Room, radiator, subs and cop-totals files are not touched.
   - `--check DIR` reports each home day's format.
   - Non-zero exit on any failure.
7. Host tests (run.sh)
   - test_convert.c: v2 day converted (values preserved, defrosting null), re-run on the output is byte-identical, input unchanged, bad file reported and absent from the output, partial tail, defrost flag read back from a v3 day, in-place refused.
   - test_server.c writes its night as a v2 file and converts it, so every existing MCP expectation runs against converted data; room and radiator v2 files still read.
8. Docs: history_format.h comments, firmware/CLAUDE.md (layout, defrost source, converter), and the step-by-step card procedure with a full backup first.
9. Build the firmware and the web app (tsc and lint on the changed files), and run the host tests.

Not in scope (flag only): MQTT/Home Assistant defrost entity, the MCP defrost series and descriptions (TASK-41), the mode icon in the page header.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-12: Implemented on feature/defrost (uncommitted).

Firmware:
- history_format.h: rec_home_t is 40 B, with defrosting at field 14 and reserved3-7. Versions are per kind (home 3, room and radiator still 2). Mask is uint32. history_check_header() returns OK / OLD_FORMAT / BAD, and every reader and the writer use it.
- Logger: records defrosting, and never writes to an old-format file. The flush task counts old-format home days at start-up (history_logger_old_format_days()).
- history_api: an old-format day answers storage "needsConversion"; READ_CHUNK_RECORDS is now 176.
- Totals backfill waits at an old-format day instead of caching zeros.
- MCP open_day uses the header check.
- Defrost source: BooleanState StateValue subscribed for heat pump nodes (9 paths, was 8), kept per endpoint in heat_pump_manager, and taken from the endpoint named "Defrost" in update_heat_pump_running().
- /api/home carries defrosting and historyNeedsConversion; heatPump.defrosting is present only when a Defrost endpoint exists.

Web:
- Defrost card in HeatPump.tsx.
- Warning on every tab in PageHeader.
- History explains a needsConversion day.
- modeSpans() returns defrost spans, drawn as a 4 px row under the mode strip with a Defrost key entry.
- History FIELDS updated.

Converter: firmware/history_convert/ (history_convert.c/.h, main.c, build.sh). It converts or copies home-* into an output directory, verifies each day before renaming it into place, refuses to run in place, drops a partial last record and says so, and has a --check mode.

Tests: run.sh runs test_convert (63 checks) and test_mcp (1854), then test_server (169), whose night is now written as v2 and converted first. All pass. Three deliberate converter bugs (header changed, defrost set to 0, in-place guard removed) were each caught. AC 8 is covered for summarise and get_series through test_server. The day chart and totals read the same field positions, which did not move; history_api.c is not host-tested.

Build: idf.py build links (heating-monitor.bin 0x254ec0) with no new warnings. tsc is clean and eslint is clean on the changed files.

Not verified on hardware, and the reason ACs 1, 2, 3, 9, 10 and 11 are unchecked: the board has not been flashed, and the adapter's TASK-4.1 (the Defrost endpoint) is not built yet. The agreed label is recorded in that task. The adapter must be re-paired once it has the endpoint, because the TagList label is read at commissioning only.

Not done, out of scope: MQTT/HA defrost entity; MCP series and descriptions (TASK-41); a defrost indicator in the page header's mode icon. The CLAUDE.md MCP note says the tools don't expose defrost yet.
<!-- SECTION:NOTES:END -->
