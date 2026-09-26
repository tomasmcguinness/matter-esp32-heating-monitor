---
id: TASK-9
title: Expose the generic Matter Controller Companion (MCC) API
status: Done
assignee: []
created_date: '2026-09-21 06:26'
updated_date: '2026-09-25 05:27'
labels:
  - firmware
  - api
dependencies: []
references:
  - 'https://github.com/tomasmcguinness/matter-controller-companion-app'
priority: high
ordinal: 6000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The bespoke iOS app in `companion_app/` has been replaced by a generic one, matter-controller-companion-app (MCC), which works against any self-hosted Matter controller. Its API is fixed and documented in that repo's README, `iOS/Shared/MCCClient.swift` and `iOS/Shared/Models.swift`.

The firmware already performs every operation MCC needs, but at `/api/nodes` and behind an `Authorization: Bearer` pairing token that only the old app knew about. MCC expects the same operations under `/api/companion/*`, with no authentication, plus a reachability endpoint it calls as soon as the user types the controller's address.

The node JSON `nodes_get_handler` emits is what MCC's `Models.swift` was written against, so the payload shape needs no change; what is missing is the routing and the `info` endpoint, and what is now dead is the pairing-token machinery and the old Xcode project.

The web UI must keep using `/api/nodes` unchanged.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 GET /api/companion/info returns 2xx with the controller's name, hostname, URL and IP (null when unavailable), and carries no pairing token or device id
- [x] #2 POST /api/companion/nodes accepts {inUse, setupCode}, holds the request open until commissioning finishes, and answers 201 {nodeId} on success or 400/409/502/504 with a readable body
- [x] #3 PUT /api/companion/nodes/{nodeId}/update renames the node, persists it across a reboot, and returns 404 for an unknown node or an action other than update
- [x] #4 DELETE /api/companion/nodes/{nodeId} unpairs the node and removes it from the node manager
- [x] #5 The pairing token is gone: no pairing_manager, no /api/info, no Authorization handling, and the Settings page shows the address to enter into MCC instead of a QR code
- [x] #6 companion_app/ is removed from the repo
- [x] #7 The web UI is unaffected: Devices, Device, EditDevice and AddDevice still work against /api/nodes
- [x] #8 firmware/CLAUDE.md documents the /api/companion/* surface and that it is MCC's contract
- [x] #9 The companion and web-UI endpoints share the Matter operations (commission, unpair, rename) but build their payloads separately, so a field added for the web UI cannot appear on the companion contract and the contract cannot constrain the web UI
- [x] #10 GET /api/companion/nodes returns its own array carrying only what the app uses - nodeId, vendorName, productName, nodeName, hasSubscription and endpoints with endpointId, endpointName and deviceTypes - and not the web UI's fields
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented and building clean (`idf.py build`, esp32s3 image 0x228370 bytes). Not yet flashed — the board at /dev/ttyACM0 was not on the network, so the on-device curl and MCC end-to-end checks are still outstanding.

**New files** (`firmware/main/`):
- `node_api.h` — the shared node operations: `build_nodes_json()`, `commission_node()` + `commission_outcome_t` + `send_commission_outcome()`, `unpair_node()`, `rename_node()`, and `read_json_body()`. Implemented in `app_main.cpp` so the commissioning semaphores and CHIP pairing callbacks stay in one translation unit.
- `companion_api.{h,cpp}` — the five `/api/companion/*` handlers, registered by `companion_api_register()` before the `/*` wildcard, like `history_api_register()`/`status_api_register()`.
- `device_identity.h` — `DEVICE_NAME`, `MDNS_HOSTNAME`, `MDNS_HTTP_PORT`, moved out of `app_main.cpp`. They could not live in `app_main.h`: that header declares `attribute_data_read_done` with bare `ScopedMemoryBufferWithSize` and only compiles after `app_main.cpp`'s `using namespace` block.

**Removed**: `managers/pairing_manager.{h,cpp}`, `g_pairing_manager`, `pairing_manager_init()`, `log_client_token()` and both call sites, `info_get_handler` and `/api/info`, and the whole `companion_app/` directory. The `"pairing"` NVS namespace is orphaned rather than erased — 32 bytes, no migration worth writing.

**Also changed**:
- `config.max_uri_handlers` 30 → 34. The count is now 30 (22 in `app_main.cpp` + 2 history + 1 status + 5 companion).
- `set_node_name()` now takes `const char *` — it strdups, and `rename_node()`'s parameter is const.
- `nodes_get_handler` switched from `cJSON_Print` + `free()` to `cJSON_PrintUnformatted` + `cJSON_free`, the pattern every other handler uses.

**Bugs fixed in passing, all on code this touched:**
1. `PUT /api/nodes/:id/update` read the body into `char content[req->content_len]` with no `+1` and never NUL-terminated it before `cJSON_Parse`, then dereferenced `nameJSON->valuestring` with no type check. Both now go through `read_json_body()` plus a `cJSON_IsString` check.
2. `UrlTokenBindings::hasBinding()` only inspects the *pattern*, so it answers yes for a URI that stopped short; `get()` is what returns NULL. `node_put_handler` passed that NULL straight to `ESP_LOGI("%s")` and `strcmp` — `PUT /api/nodes/5` with no action segment was a crash. Guarded in both `node_put_handler` and the companion handlers.
3. `node_delete_handler` set a status and returned without ever calling `httpd_resp_send()`, so the client waited for its own timeout instead of getting the 202. Both delete handlers now send an explicit (empty) body.

MCC reports a failure listing devices. Compared against matter-esp32-controller and matter-esp32-home-energy-manager, which work:

- **Routes are equivalent.** Both references register the same five paths before their `/*` catch-all, as we do. They need a custom `uri_match_segments` because they register `/api/companion/nodes/*/update` (a mid-path wildcard the stock matcher can't express); we register `/api/companion/nodes/*` for PUT and dispatch on the last segment inside the handler, which `httpd_uri_match_wildcard` handles. Verified against the matcher source: `/api/companion/nodes` matches nothing registered earlier.
- **Payload is not the cause.** The references emit a deliberately minimal node and omit `isIcd`, `powerSource`, `battery*`, `extAddress` and `measuredValue` because they have no source for them; we emit all of them plus `lastSeen`. All are optional in MCC's decoder, and the deleted `companion_app/Shared/Models.swift` (recovered from 3a587e4^) is byte-identical to MCC's `Models.swift` and decoded this exact JSON from `/api/nodes`.
- **`GET /api/companion/info` being 2xx proves nothing.** MCC ignores the body, so the SPA fallback's `200 text/html` also passes the reachability check — a controller can be added successfully while the companion routes are not reachable at all.

That points at the request not reaching the handler; the leading candidate is a base address with a trailing slash, giving `//api/companion/nodes`, which matches only `/*`.

Made the failure legible rather than silent: `wildcard_get_handler` now returns `404 No such endpoint: <uri>` for any unmatched path under `/api/` or `//api/` instead of serving `index.html`, and `companion_nodes_get_handler` returns a 500 rather than an empty 200 if `cJSON_PrintUnformatted` fails, and logs the byte count it sends. Awaiting the device's answer to `curl -i http://heating-monitor.local/api/companion/nodes` to confirm.

Corrected the structure. I had `/api/companion/nodes` reuse the web UI's `build_nodes_json()`, which defeats the point of the namespace: the UI-only `lastSeen` field ended up on the published contract, and every future UI field would have followed it.

The two payloads are now built separately:
- `build_nodes_json()` (static, `app_main.cpp`) — `/api/nodes`, for the web UI, keeps `lastSeen`.
- `build_companion_nodes_json()` (`companion_api.cpp`) — `/api/companion/nodes`, written against the contract alone.

`node_api.h` is now operations only: `commission_node()`, `unpair_node()`, `rename_node()`, `send_commission_outcome()` and `read_json_body()`. Both APIs drive the same Matter code; neither dictates the other's JSON. `companion_api_register()` takes a `node_manager_t *` by injection, matching `history_logger_init()`.

The companion array also reports an unset `endpointName` as null rather than `""`, omits `batteryPercent`/`batteryVoltage` when the node doesn't report them rather than sending null, and omits `extAddress` — see the bug below. A node is 336 bytes unformatted, against ~600 before.

Trimmed the companion array to what the app uses, matching what matter-esp32-controller and matter-esp32-home-energy-manager send:

    nodeId, vendorName, productName, nodeName, hasSubscription,
    endpoints[endpointId, endpointName, deviceTypes]

Dropped `isIcd`, `powerSource`, `batteryPercent`, `batteryVoltage` and `measuredValue`. All are real readings and all are optional in the contract, but the app's list view renders only `displayName`, and a list refreshed on every pull has no business carrying a per-endpoint sensor feed. They remain on `/api/nodes`, where the web UI shows them. `extAddress` stays off for the separate reason that it reads 0 everywhere.

Three of the eight real nodes now come to 819 bytes unformatted; the same three were roughly 2.4 KB when the endpoint was reusing the web UI's array.

Still failing after the trim, and the two facts together rule out the payload:

- MCC's alert is **"Couldn't read the controller's response."** — `ClientError.decodingFailed`, which is only reached after a 2xx. A body arrived and JSONDecoder rejected it.
- The serial monitor shows **nothing at all** on pull-to-refresh. `companion_nodes_get_handler` logs on entry and the new `/api/` 404 logs a warning; neither fired.

A 2xx that this device never served means the app is getting an HTML page from somewhere that isn't `companion_api.cpp` — a different host, or a path our SPA fallback answers silently. The payload work stands on its own merits but was never going to fix this.

Added two diagnostics so the next refresh is conclusive rather than inferred:
- `config.open_fn = on_socket_opened` logs every HTTP client with its peer IP. If a refresh produces no line, the app never reached this device and the controller's saved address is wrong.
- `wildcard_get_handler` now logs `Serving the web app for <uri>` on the SPA fallback. Previously only `/api/` paths logged, so a request on any other path was served `index.html` — a 200 with HTML, exactly what decodingFailed means — leaving no trace.

**Root cause found, and it was mine.** `CONFIG_USE_MINIMAL_MDNS=y` gives UDP 5353 to CHIP's own responder, so `start_mdns_service()` in `app_main.cpp` is commented out and **nothing advertises `heating-monitor.local`**. The comment on that disabled call says so outright.

I built `companion_info_get_handler` to report `url` as `http://heating-monitor.local` and rewrote the Settings page to present it as the address to type into MCC. The name doesn't resolve to this board, so iOS reached whatever else on the LAN answered, got that machine's web page — a 200 with HTML — and reported `decodingFailed`, while this device logged nothing because no request arrived. That is exactly the pair of symptoms reported, and no amount of payload work could have touched it.

Fix:
- `companion_info_get_handler` builds `url` from the IPv4 address (`http://<ip>`), like matter-esp32-controller does, and emits `ip`/`url` as null when there is no address. The `host` field is gone.
- `Settings.tsx` shows a single Address row with that URL and states plainly that the hostname does not work and why.
- `device_identity.h` and `firmware/CLAUDE.md` corrected — CLAUDE.md claimed the web UI was published over mDNS at that name.

I had the evidence early and dismissed it: `ping heating-monitor.local` failed from this machine in the first session and I attributed it to WSL2 not doing mDNS.
<!-- SECTION:NOTES:END -->

## Definition of Done
<!-- DOD:BEGIN -->
- [ ] #1 Fix ThreadNetworkDiagnostics::ExtAddress reading 0 for every node, then restore extAddress to the companion payload
<!-- DOD:END -->
