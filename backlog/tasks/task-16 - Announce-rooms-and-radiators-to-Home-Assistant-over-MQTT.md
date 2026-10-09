---
id: TASK-16
title: Announce rooms and radiators to Home Assistant over MQTT
status: Done
assignee: []
created_date: '2026-10-01 08:18'
updated_date: '2026-10-01 08:21'
labels:
  - firmware
  - mqtt
dependencies: []
references:
  - firmware/main/mqtt/ha_discovery.cpp
  - firmware/main/managers/calculations_manager.cpp
  - firmware/main/app_main.cpp
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Only the home device is announced to HA properly. The radiator announcement in radiators_post_handler has no device ids, so HA discards it; it is not retained and is never re-sent. Rooms are not announced at all. Rooms and radiators should announce the way the home does, so HA can log their readings alongside the local SD history.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Each room and radiator is announced as its own HA device, retained, linked to the Heating Monitor via via_device
- [x] #2 Announcements are re-sent on every MQTT connect and on room/radiator create, update and reset
- [x] #3 Deleting a room or radiator clears its retained discovery config
- [x] #4 Room state publishes current_temperature, measured/predicted heat loss and heat_loss_difference, with null when there is no reading
- [x] #5 Radiator state publishes flow, return, mean water temperature and output on every update, with null when there is no reading
- [x] #6 Firmware builds
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Generalise ha_discovery so the home, each room and each radiator are each announced as their own device. Ids are MAC-based (room/radiator id appended), with via_device pointing at the home.
2. Have the HTTP handlers announce or remove single rooms and radiators when they are created, updated, deleted or reset. Remove the old inline radiator discovery block.
3. On MQTT connect, announce the home immediately. Rooms and radiators are announced from update_home() via a pending flag, so the lists are never walked on the MQTT task.
4. Publish null-aware room and radiator state payloads through the shared state-topic helpers.
5. Document in firmware/CLAUDE.md.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
The plan was to walk the room and radiator lists in ha_discovery_announce_all() on the MQTT task. firmware/CLAUDE.md marks that as a use-after-free risk (it is why the history logger snapshots), so connect now only flags rooms and radiators. update_home() announces them on its next run, at most about a minute later given the max-60s subscriptions.

The old radiator config topic homeassistant/device/radiators/<mqtt_name>/config was never retained, so nothing is left behind at the broker.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Rooms and radiators are now announced to Home Assistant the same way the home is.

**Discovery (`mqtt/ha_discovery.{h,cpp}`)**
- Each room and radiator is its own HA device: `heating_monitor_<mac>_room_<id>` or `_radiator_<id>`, linked to the Heating Monitor with `via_device`. Rooms set `sa` to the room name.
- Configs are retained on `homeassistant/device/<id>/config` and share the `heating_monitor/status` availability topic.
- Room entities: temperature, measured heat loss, predicted heat loss, heat loss vs predicted (%). Radiator entities: flow, return, mean water temperature, heat output (now W/power; it was mislabelled °C).
- Ids are keyed on the room or radiator id, so a rename overwrites the config instead of orphaning entities.
- New API: `announce_room` / `announce_radiator`, `remove_room` / `remove_radiator`, `announce_rooms_and_radiators`, `announce_pending`, and state-topic helpers with an id fallback when the MQTT name is empty.

**When announcements are sent**
- On connect: the home immediately; rooms and radiators on the next `update_home()`. The lists are never walked on the MQTT task.
- From the HTTP handlers: radiator POST, PUT and DELETE; room POST, PUT and DELETE; and reset. The broken inline radiator discovery block is removed.

**State (`calculations_manager.cpp`)**
- Radiators now publish on every update, including when they are not in a room or have no flow/return reading. Flow and return are null when 0. Mean water temperature and output are null unless they were worked out on that pass.
- Room temperature is null when 0. Measured heat loss and the difference are null without a room reading, an outdoor reading or any radiators.

**Docs:** firmware/CLAUDE.md has a new MQTT section.

**Tests:** the full `idf.py build` passes, and the changed files compile without warnings. Not yet flashed or checked against a broker or HA.
<!-- SECTION:FINAL_SUMMARY:END -->
