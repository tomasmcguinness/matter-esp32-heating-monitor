---
id: TASK-46
title: >-
  /api/nodes stalls: JSON responses exhaust internal RAM and starve the W5500's
  DMA buffers
status: Done
assignee:
  - '@claude'
created_date: '2026-11-25 20:08'
updated_date: '2026-11-25 20:13'
labels:
  - firmware
  - memory
  - http
dependencies: []
priority: high
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Every GET /api/nodes (15.7 KB) stalls after 0-7 KB and the client times out, while larger streamed endpoints are fine. The console shows, for every outgoing frame during the send: `spi_master: setup_dma_priv_buffer: Failed to allocate priv TX buffer` then `w5500.mac: emac_w5500_transmit: write frame failed`. nodes_get_handler builds the whole node list as a cJSON tree (thousands of small allocations) and a 15.7 KB string; both are under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16384) so they come from internal RAM, which has ~36 KB free in normal running, and both are held through the send. The SPI driver's per-frame DMA bounce buffer (internal RAM) then cannot be allocated, frames are dropped and TCP stalls. Same failure mode as the one 00c3854 addressed for TLS/CASE.

Decided with the user 2026-11-25: do both fixes: stream /api/nodes one node at a time, and route all cJSON allocations to PSRAM via cJSON_InitHooks.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 GET /api/nodes completes in well under a second, repeatedly, with the same JSON as before
- [x] #2 /api/nodes is sent one node at a time, so its peak memory is one node's JSON rather than the whole list
- [x] #3 cJSON allocates from PSRAM (falling back to internal RAM only if PSRAM is exhausted), set up before any cJSON use
- [x] #4 No 'Failed to allocate priv TX buffer' or 'write frame failed' on the console during /api/nodes, /api/rooms, /api/radiators, /api/home and an MCP list_rooms call
- [x] #5 Other JSON endpoints and the web UI still work; firmware builds with IDF v5.5.5
- [x] #6 firmware/CLAUDE.md records the internal-RAM rule for HTTP responses
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-25: build_nodes_json() split into build_node_json(node); nodes_get_handler sends '[', each node printed/sent/freed, ']' as chunks. app_main() installs cJSON_InitHooks first thing: cjson_malloc = heap_caps_malloc_prefer(SPIRAM|8BIT, then DEFAULT), cjson_free = heap_caps_free. Comments in companion_api.cpp and node_api.h, and CLAUDE.md (new 'Internal RAM is what Ethernet runs on' section + node array note), updated. IDF v5.5.5 build green. Not flashed: the flash needs the user's explicit OK. To verify after flashing: time /api/nodes x5, parse JSON (27 nodes), watch console for 'priv TX buffer' / 'write frame failed' while calling /api/nodes, /api/rooms, /api/radiators, /api/home and MCP list_rooms.

2026-11-25 verified on the board after flashing: /api/nodes 0.13-0.20 s on every request (was a 20 s timeout after 0-7 KB), 15,670 B, 27 nodes / 61 endpoints, same keys. Three rounds of /api/nodes, /api/rooms, /api/radiators, /api/home, /api/radiators/14, /api/history/balance and MCP list_rooms all completed (0.02-0.45 s) with 0 'priv TX buffer' and 0 'write frame failed' lines over 759 console lines; no reset (uptime 45 -> 85 s). Internal RAM free ~35 KB afterwards.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
GET /api/nodes stalled on every request: nodes_get_handler built the whole node list as a cJSON tree plus a 15.7 KB string in internal RAM (~36 KB free) and held it through the send, so the W5500 SPI driver's per-frame DMA bounce buffer could not be allocated and frames were dropped. Fixed two ways: /api/nodes now streams one node at a time (build_node_json), and cJSON allocates from PSRAM via cJSON_InitHooks set at the top of app_main. firmware/CLAUDE.md gained a section on why internal RAM matters for Ethernet. Verified on the board: /api/nodes 0.13-0.20 s, all JSON endpoints and MCP list_rooms fast, no frame errors on the console.
<!-- SECTION:FINAL_SUMMARY:END -->
