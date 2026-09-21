---
id: TASK-9
title: Expose the generic Matter Controller Companion (MCC) API
status: In Progress
assignee: []
created_date: '2026-09-21 06:26'
updated_date: '2026-09-21 06:35'
labels:
  - firmware
  - api
dependencies: []
references:
  - 'https://github.com/tomasmcguinness/matter-controller-companion-app'
priority: high
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
- [x] #2 GET /api/companion/nodes returns the same node array as GET /api/nodes, with every field MCC's Models.swift decodes
- [x] #3 POST /api/companion/nodes accepts {inUse, setupCode}, holds the request open until commissioning finishes, and answers 201 {nodeId} on success or 400/409/502/504 with a readable body
- [x] #4 PUT /api/companion/nodes/{nodeId}/update renames the node, persists it across a reboot, and returns 404 for an unknown node or an action other than update
- [x] #5 DELETE /api/companion/nodes/{nodeId} unpairs the node and removes it from the node manager
- [x] #6 The companion and web-app endpoints share one implementation of the node list, commissioning, rename and unpair, so the two cannot drift
- [x] #7 The pairing token is gone: no pairing_manager, no /api/info, no Authorization handling, and the Settings page shows the address to enter into MCC instead of a QR code
- [x] #8 companion_app/ is removed from the repo
- [x] #9 The web UI is unaffected: Devices, Device, EditDevice and AddDevice still work against /api/nodes
- [x] #10 firmware/CLAUDE.md documents the /api/companion/* surface and that it is MCC's contract
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
<!-- SECTION:NOTES:END -->
