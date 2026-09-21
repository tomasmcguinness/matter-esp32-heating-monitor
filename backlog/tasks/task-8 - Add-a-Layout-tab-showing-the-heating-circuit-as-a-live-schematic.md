---
id: TASK-8
title: Add a Layout tab showing the heating circuit as a live schematic
status: In Progress
assignee: []
created_date: '2026-09-19 05:45'
updated_date: '2026-09-19 05:57'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The web UI presents the heating system only as tables — Rooms lists rooms, Radiators lists radiators, Home shows the heat meter — so nothing conveys how they connect. A flow/return schematic makes the circuit legible at a glance: which radiators are hot, which rooms are under target, and what the heat meter is delivering into the whole thing.

Add a `Layout` tab, sitting straight after Home, that renders the system as a schematic driven by the device's own room and radiator data rather than hard-coded content. The visual design comes from a static mock-up supplied by the user (`layout-schematic.html`): a flow trunk across the top, a return trunk across the bottom, one radiator symbol dropped between them per emitter, and a heat-meter panel on the left.

Agreed shape:
- One column per radiator, grouped under its room, with a bracket over rooms that have more than one. The mock-up assumes one radiator per room, but `room_t` allows several.
- Values update live from the existing WebSocket channels, not by polling.
- No anomaly/alert markers. Nothing in the firmware produces an ML verdict yet; that is separate work.

Blocking data gap: `GET /api/rooms` returns rooms without their radiators, and `GET /api/radiators` returns radiators without their room (`radiator_t::room_id` is declared but never assigned, so the only real link is `room->radiators[]`). There is no pair of calls the page can join today, so `/api/rooms` needs to carry its radiators.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A Layout tab appears in the navbar between Home and Rooms, routing to /layout, and a browser reload or deep link on /layout serves the page
- [ ] #2 GET /api/rooms returns a radiators array on every room, using the same field names the single-room endpoint already emits (radiatorId, name, type, flowTemp, returnTemp, currentOutput); a room with no radiators returns an empty array rather than omitting the key
- [ ] #3 Existing fields on GET /api/rooms are unchanged in name, type and value
- [ ] #4 Every configured room is drawn, ordered by name, with its radiators ordered by id, and a room with more than one radiator shows one column per radiator under a single bracketed room header
- [ ] #5 A room with no radiators still shows its header rather than disappearing from the schematic
- [ ] #6 A room with no temperature sensor bound, and a radiator with no flow or return reading, render as a dash rather than as 0 or a blank
- [ ] #7 With no rooms configured, the page shows the same empty-state alert the other pages use
- [ ] #8 Radiator flow temperature, return temperature and heat output, and room temperature, all update on screen without a page reload
- [ ] #9 The heat meter panel shows flow temperature, return temperature and output matching what the Home tab reports for the same instant
- [ ] #10 The page's styles are scoped so they do not affect any other page; in particular Bootstrap alerts on Rooms and Devices still render correctly after visiting Layout
- [ ] #11 The compiled web app still emits exactly index.html, app.css and app.js, so main/CMakeLists.txt needs no change
- [ ] #12 npm run lint passes and idf.py build succeeds
- [ ] #13 firmware/CLAUDE.md is updated to mention the Layout route alongside the other routes it lists
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented; everything verifiable off-hardware passes. Remaining ACs need the device.

Firmware (`main/app_main.cpp`):
- `rooms_get_handler` now emits a `radiators` array per room, built inside the loop that already walks `room->radiators[]` for `heatInput`, so there are no extra `find_radiator()` calls. Fields match `room_get_handler` exactly. The array is created before the loop, so a room with no radiators returns `[]`.
- The `radiator` WebSocket push carries `currentOutput`, added after `update_radiator_outputs()` runs — before that call it would carry the previous reading.

Web app: new `Layout.tsx` + `Layout.css`, nav item and route in `App.tsx`.
- One column per radiator, grouped under the room, bracket drawn only when a room has more than one. Room header centred over its group.
- Geometry laid out from the radiator count: `COL_PITCH` 84 within a group, `GROUP_GAP` 40 between rooms; canvas width and trunk end derived from the last column.
- Reuses `Temperature.tsx` and `Power.tsx`. A local `reading()` maps an exact 0 to undefined so unreported sensors dash instead of showing 0.0 °C — the transient fields start at zero and the payload carries no has_* flag to distinguish them. `kilowatts()` is the one new formatter, for the meter's mW → kW headline.
- Live via the existing `home`, `radiator` and `room` channels. Patching happens inside the `setRooms` updater, not over a closed-over list.

Verified:
- `npx eslint src/Layout.tsx src/App.tsx` clean. (Repo-wide `npm run lint` has pre-existing failures in other files; untouched.)
- `npm run build` passes; still exactly index.html/app.css/app.js, with Layout.css merged into app.css, so `WEB_APP_FILES` needs no change — AC #11.
- `idf.py build` passes, 57% of the app partition free. No new warnings.
- Server-rendered the component against a sample 8-room/11-radiator system to check geometry: brackets span only the multi-radiator rooms, headers centre correctly (Living Room 834 over 792/876; Sitting Room 1332 over 1248/1416), a room with no radiators keeps its header and shows the placeholder, and an all-zero radiator dashes flow, return and watts — ACs #4, #5, #6.

Still needs the device: #1 (deep-link reload), #8 (live updates), #9 (meter matches Home), #10 (Bootstrap alerts elsewhere unaffected), and confirming #2 against real `/api/rooms` output.

---

Thread Network removal (same branch, at the user's request). They deleted `ThreadNetwork.tsx` and its nav item/route; this removed everything that existed only to serve it:
- `vis-network` and `vis-data` from package.json. The bundle had already dropped 890 KB → 370 KB when the component was deleted, since Vite tree-shook them then; removing the entries is dependency hygiene, not a further size win.
- `POST /api/network` — `network_post_handler`, its `httpd_uri_t` and its `httpd_register_uri_handler` call. The deleted component was its only caller.
- The `NeighborTable` branch of `attribute_data_cb`, reachable only via the read that handler scheduled, and with it the `network` WebSocket channel. 153 lines net out of `app_main.cpp`.

Deliberately kept: `ThreadNetworkDiagnostics::ExtAddress`. It is subscribed for every node, stored on `matter_node_t::ext_address`, persisted in the node NVS blob and reported by `GET /api/nodes` as `extAddress` — it is part of the node record, not of the topology page.

`firmware/CLAUDE.md` records both the new Layout route and what the Thread Network removal took with it.
<!-- SECTION:NOTES:END -->
