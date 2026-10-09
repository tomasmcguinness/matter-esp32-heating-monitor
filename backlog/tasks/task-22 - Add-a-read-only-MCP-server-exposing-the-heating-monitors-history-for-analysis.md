---
id: TASK-22
title: Add a read-only MCP server exposing the heating monitor's history for analysis
status: In Progress
assignee: []
created_date: '2026-10-09 09:18'
updated_date: '2026-10-09 09:50'
labels:
  - firmware
  - mcp
dependencies:
  - TASK-20
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
An AI assistant should be able to analyse the heat pump's cycling, COP and the effect of heating-curve changes from the monitor's recorded history. The system is a Kronoterm ADAPT heat pump with heat and electricity metering.

The data lives on the ESP32's SD card and is served by its HTTP API (GET /api/history, /api/history/dates). The MCP server is a separate process that calls that API; it never writes to the device or the heat pump.

Decisions agreed with the user:
- The device's history endpoint gains a way to return every slot of a day (points=0), because cycle detection needs raw 5 s samples and the chart endpoint otherwise averages to at most 2000 points. The MCP server caches completed days so the large response is paid for once per day.
- TypeScript with the official MCP SDK, in a new top-level mcp_server/ directory.
- Streamable HTTP transport guarded by a bearer token from an env var, plus stdio for local testing.
- Tools: list_series, get_series (auto interval to <=1000 points, hard cap 5000 per series), summarise_cycles (compressor runs from electrical input, split on mode change, implausible heat samples excluded), compare_periods.
- Timestamps ISO 8601 in and out, timezone parameter defaulting to Europe/London.

Known gaps, to be reported not papered over: target flow temperature is not recorded; mode is heating / hot water only (no defrost) and exists only for days recorded after TASK-20; per-room and per-radiator series are not exposed in this task.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 GET /api/history?points=0 returns every slot of the day unaveraged, and other points values behave as before
- [x] #2 The server exposes list_series, get_series, summarise_cycles and compare_periods, all marked read-only, with units in field names or descriptions
- [x] #3 get_series auto-picks an interval giving at most 1000 points and returns an error suggesting a coarser interval when a request would exceed 5000 points per series
- [x] #4 summarise_cycles detects runs from electrical input, merges short gaps, drops short runs, splits on mode change, and excludes implausible heat samples with a per-run count
- [x] #5 Pump-only heat with near-zero electricity is not counted as a run
- [x] #6 HTTP requests without the correct bearer token are rejected, and the server refuses to start in HTTP mode with no token configured
- [x] #7 The server runs over stdio for local testing
- [x] #8 Unit tests cover cycle detection on the synthetic night fixture, the point caps and auth rejection, and pass
- [x] #9 A README covers env vars, running locally and exposing the server over HTTPS for use as a custom connector
- [ ] #10 The server has been run against the real device and a raw day fetch timed
- [ ] #11 The firmware serves the four tools at POST /mcp and Claude Code on the LAN can connect and call them
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented 2026-10-09. Firmware: requested_points() in history_api.c returns 0 for points=0, which stream_file() already treats as stride 1. MCP server in mcp_server/ (TypeScript, @modelcontextprotocol/sdk 1.32, zod 4, vitest): time.ts (ISO/timezone), monitor.ts (GET-only device client, one request at a time, completed days cached in memory and on disk), series.ts (catalogue, interval planning, resampling), cycles.ts (run detection and summary, pure), server.ts (the four tools), http.ts (stateless Streamable HTTP on POST /mcp behind a bearer token), index.ts (HTTP or --stdio).

Verified: 47 tests pass (synthetic night, merge/drop rules, point caps, auth rejection, device client against a fake fetch, tools end to end over an in-memory transport); tsc builds; built server answers tools/list over stdio; firmware builds. NOT verified: criterion 1 and 10 - nothing flashed and the server has never talked to the real device, so points=0 is untested on hardware and a raw day fetch has not been timed. The claude.ai connector flow is untested too.

Choices made without asking: target flow at stop is omitted (not recorded); HEAT_PUMP_RATED_OUTPUT_W defaults to 8000 as a placeholder; a 31-day cap per call (MAX_RANGE_DAYS); an include_runs flag on summarise_cycles; heat with zero electrical input counts as COP > 10 and is excluded; a run's COP divides energies over the samples whose heat was kept; mode gaps inside a run are filled from neighbouring samples and mode stretches shorter than min_run_seconds are folded into a neighbour.

claude.ai: sending a fixed Authorization header from a custom connector is a beta 'Request headers' option being rolled out gradually (per claude.com/docs/connectors/custom/add-unlisted, found by web search 2026-10-09). If the account lacks it, the server would need OAuth to be added as a connector.

Direction changed 2026-10-09: the user wants the MCP server on the ESP32 itself, for Claude Code on the LAN, with authentication skipped for now. Built in firmware/main/mcp/: mcp_server.c (stateless Streamable HTTP on POST /mcp, JSON-RPC, the four tools, GET answers 405), mcp_tools.json (tool definitions, embedded), mcp_cycles (streaming run detection over sums), mcp_series (series table and point caps), mcp_time (ISO 8601). Registered before the wildcard; max_uri_handlers 34 -> 36. Differences from the Node server: no bearer token; timezone is Europe/London or UTC only; rated output is a tool argument rated_output_w (default 8000) instead of an env var; reads the day files directly so points=0 is not used.

Verified for the firmware server: idf.py build links; firmware/test_host/run.sh passes under ASan/UBSan - 117 checks on the plain-C modules (synthetic night, merge/drop, flicker, plausibility, caps, time) and 111 on the real mcp_server.c driven through stand-in HTTP headers against a fixture day file, every response parsed back as JSON; the official MCP SDK client completed initialize / tools/list / tools/call against that host build through an HTTP shim (POST 200, notification 202, GET 405). NOT verified: nothing flashed, so it has never run on the board - httpd header limits with Claude Code's real requests, PSRAM allocation, SD read time for multi-day ranges and stack use on the httpd task are all untested.

The Node server in mcp_server/ is left in place as the off-device alternative (bearer token, HTTPS for claude.ai). The user has not said whether to keep it.
<!-- SECTION:NOTES:END -->
