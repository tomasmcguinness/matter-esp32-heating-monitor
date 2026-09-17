---
id: TASK-5
title: Show a "last seen" time for each device on the Devices page
status: Done
assignee:
  - tomas@tomasmcguinness.com
created_date: '2026-09-15 14:19'
updated_date: '2026-09-15 18:40'
labels:
  - firmware
  - web-app
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A device that has stopped reporting is currently indistinguishable from one that is reporting steadily: the Devices page shows a subscription tick or cross, but a subscription can look healthy while the device behind it has been silent for hours. Sensors are subscribed min 0 / max 60, so a healthy device should be heard from at least once a minute.

Give the user that information by recording, for each Matter node, the time of the most recent report from it, and surfacing it as a tooltip on the subscription icon in the Devices table.

The timestamp is deliberately **transient** — held in RAM only, never written to NVS — so after a restart it reads as "not seen yet" rather than resurrecting a stale time from before the reboot.

Relevant code:
- `main/app_main.cpp` — `attribute_data_cb` is where every report from a device arrives; `nodes_get_handler` builds the `GET /api/nodes` payload.
- `main/managers/node_manager.h/.cpp` — `matter_node_t` and its NVS serialisation.
- `main/storage/time_sync.h` — `clock_is_valid()`; before the first SNTP sync `time(NULL)` is in 1970, so any timestamp taken then is meaningless.
- `html_app/src/Devices.tsx` — the subscription tick/cross in the node row.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Each Matter node records the time of the most recent report received from it, updated whenever a device reports
- [x] #2 The recorded time is held in RAM only and is not written to or read from NVS, so it resets on restart
- [x] #3 No time is recorded before the system clock has been set by SNTP, so 1970 timestamps never reach the UI
- [x] #4 GET /api/nodes reports the last seen time for every node, and reports it as null when the node has not been heard from since restart
- [x] #5 Hovering the subscription icon on the Devices page shows the subscription state together with the last seen date and time in local format
- [x] #6 Hovering the subscription icon for a node not heard from since restart shows that it has not been seen, rather than a date
- [x] #7 firmware/CLAUDE.md is updated where it states that no last-updated timestamp is recorded
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. `main/managers/node_manager.h` — add `uint32_t last_seen;` to `matter_node_t`, commented as transient and as `uint32_t` deliberately (a 32-bit load is atomic on the S3, so the HTTP task cannot read a torn value written by the CHIP event loop). Declare `esp_err_t mark_node_seen(node_manager_t *manager, uint64_t node_id, uint32_t timestamp);`.

2. `main/managers/node_manager.cpp` — implement `mark_node_seen`. The timestamp is passed in rather than read inside, so `node_manager` keeps no dependency on `time_sync`, matching the existing `set_*` style. No NVS work: `save_nodes_to_nvs` serialises field by field and `load_nodes_from_nvs` `calloc`s each node, so the field is absent from the blob and starts at 0 after a restart with nothing further to do.

3. `main/app_main.cpp`, `attribute_data_cb` — stamp at the top, before the null-`data` early return: a status-only report still proves the device answered. Guard with `clock_is_valid()` so nothing is recorded before the first SNTP sync. This covers subscription reports and interview reads alike.

4. `main/app_main.cpp`, `nodes_get_handler` — emit `lastSeen` next to `hasSubscription`, as unix seconds, or `null` when `last_seen == 0`, following the existing null-vs-value pattern used for the battery fields.

5. `html_app/src/Devices.tsx` — wrap the tick/cross in a single span carrying a `title` that combines the subscription state with the last seen time, formatted with `toLocaleString()`. Both states get the tooltip, not just the cross. Null renders as "not seen since restart".

6. `firmware/CLAUDE.md` — update the "Freshness is the known gap" paragraph, which states that no last-updated timestamp is recorded.

Out of scope, offered as a follow-up: the three `SubscriptionPill`s on the Home screen, which are fed by `build_home_json`, not `/api/nodes`.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented as planned, with two deviations worth recording:

1. `subscriptionTitle` in `Devices.tsx` takes a narrow structural type (`{ hasSubscription?: boolean, lastSeen?: number | null }`) rather than the `any` the rest of the file uses. `any` there was the only new ESLint error the change introduced; the narrow type removes it and the call site still passes the untyped `n` fine. The file's 18 remaining lint errors are pre-existing (repo-wide baseline is 208).

2. `#include <ctime>` added to `app_main.cpp`. `time()` was referenced only inside comments before this, so the header was arriving transitively at best.

No NVS work was needed, as anticipated: `save_nodes_to_nvs` serialises field by field and `load_nodes_from_nvs` `calloc`s each node, so `last_seen` is absent from the blob and starts at 0 after a restart with nothing further to do. The same is true of `add_node`, which memsets.

**Known limitation, not addressed:** the Devices page fetches `/api/nodes` once on mount, so the tooltip is only as fresh as the last page load -- it does not tick while the page is open. Making it live would mean either a poll or putting the node list on the existing websocket, neither of which the acceptance criteria asked for.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
## What changed

A Matter node now records when it was last heard from, and the Devices page surfaces it as the subscription icon's tooltip, so a healthy-looking subscription can be told apart from one whose device has gone silent.

**Firmware**
- `node_manager.h` — `matter_node_t` gains `uint32_t last_seen` (unix seconds, 0 = not heard from). `uint32_t` rather than `time_t` is deliberate: the CHIP event loop writes it and the httpd task reads it unlocked, and a 32-bit load cannot tear on this target where a 64-bit one can.
- `node_manager.cpp` — `mark_node_seen(manager, node_id, timestamp)`. The timestamp is a parameter, so `node_manager` keeps no dependency on `time_sync`, matching the existing `set_*`/`mark_*` style.
- `app_main.cpp` — `attribute_data_cb` stamps the node on every report. Placed *before* the null-`data` early return, because a report carrying only a status is still the device answering us. Guarded by `clock_is_valid()`, so nothing is recorded before the first SNTP sync rather than recording a 1970 date.
- `app_main.cpp` — `nodes_get_handler` emits `lastSeen` next to `hasSubscription`, as unix seconds or `null`, following the null-vs-value pattern the battery fields already use.

**Web app**
- `Devices.tsx` — the tick/cross is wrapped in one span carrying a `title` combining state and time: "Subscribed — last seen 15/09/2026, 14:02:11", or "No active subscription — not seen since restart". Both states get the tooltip; previously only the cross had one.

**Docs**
- `firmware/CLAUDE.md` — the "Freshness is the known gap" paragraph was stating the opposite of what the code now does. Rewritten to record that the gap is closed for the device list but still open for the history, and that the stamp is per *node*, not per endpoint — `endpoint_entry_t` still has no timestamp, which is what the records' reserved `age_s` field would need.

## Why transient

`last_seen` is absent from the NVS blob on purpose. Persisting it would mean a freshly booted controller showing a last-seen time from before the reboot, which reads as "this device is fine" when in fact nothing has been heard from it yet. Reading 0 after a restart, surfaced as `null` and rendered "not seen since restart", is the honest answer.

## Tests

- `npm run build` (tsc + vite) — clean.
- `npx eslint src/Devices.tsx` — 18 errors, all pre-existing; the change adds none.
- `idf.py build` — succeeded through link and image generation (`heating-monitor.elf`, `heating-monitor.bin`, 47% of the app partition free). No new compiler warnings; the `nodiscard` and unused-function warnings in `app_main.cpp` are pre-existing.
- Not verified on hardware — no device was flashed, so the tooltip has not been seen against a live node.

## Follow-ups (not created)

1. The three `SubscriptionPill`s on the Home screen show subscription state but no last-seen time. They are fed by `build_home_json`, not `/api/nodes`, so they need their own payload change.
2. The Devices page fetches `/api/nodes` on mount only, so the tooltip does not update while the page is open.
3. A per-endpoint stamp would let the history records' reserved `age_s` field be filled, which is the part of the freshness gap this task leaves open.
<!-- SECTION:FINAL_SUMMARY:END -->
