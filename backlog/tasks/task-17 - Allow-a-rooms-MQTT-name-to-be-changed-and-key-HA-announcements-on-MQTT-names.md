---
id: TASK-17
title: 'Allow a room''s MQTT name to be changed, and key HA announcements on MQTT names'
status: Done
assignee: []
created_date: '2026-10-01 09:35'
updated_date: '2026-10-01 09:37'
labels:
  - firmware
  - mqtt
  - web-app
dependencies:
  - TASK-16
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The Update Room page cannot change a room's MQTT name: PUT /api/rooms/:id ignores it, update_room() has no parameter for it, and GET /api/rooms/:id does not return it. The HA announcements from TASK-16 key devices and entities on the room or radiator id. They should use the MQTT name instead, setting default_entity_id so entity ids follow it. When an MQTT name changes, the device under the old name must be removed so it is not left orphaned in HA.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 GET /api/rooms/:id returns mqttName
- [x] #2 PUT /api/rooms/:id accepts an optional mqttName, which update_room() stores and persists
- [x] #3 The Update Room page shows and edits the MQTT name
- [x] #4 Room and radiator HA device ids, unique_ids and default_entity_ids are built from the MQTT name, falling back to the id when it is empty
- [x] #5 Changing a room's or radiator's MQTT name removes the old HA device and announces the new one; deleting removes the right device
- [x] #6 Firmware and web app build
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Add an optional mqtt_name to update_room() (NULL keeps the current one). Have room GET return mqttName and room PUT accept it.
2. Add an MQTT name field to EditRoom.tsx, lowercased on save like AddRoom.
3. In ha_discovery, key child device ids on the slugified MQTT name, falling back to the id. Set default_entity_id per entity. Make the remove functions take the MQTT name.
4. In the room and radiator PUT handlers, copy the old MQTT name first; when it changes, remove the old device before announcing. Pass the MQTT name to the DELETE handlers' removals.
5. Update firmware/CLAUDE.md.
<!-- SECTION:PLAN:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
The Update Room page can now change a room's MQTT name, and HA announcements are keyed on MQTT names.

**Room MQTT name**
- `update_room()` takes an optional `mqtt_name`. NULL keeps the current name, so an older client that omits it doesn't wipe it. NVS already persisted the field.
- `GET /api/rooms/:id` returns `mqttName`, and `PUT` accepts it.
- `EditRoom.tsx` has an MQTT Name field, lowercased on save like `AddRoom.tsx`.

**Announcements**
- Device ids are `heating_monitor_<mac>_room_<mqtt_name>` and `_radiator_<mqtt_name>`, with the id as the fallback when there is no MQTT name. The kind stays in the id because a room and its radiator usually share a name.
- Each entity gets a `default_entity_id`: `sensor.<name>_<key>` for rooms and `sensor.<name>_radiator_<key>` for radiators. Names are slugified to `[a-z0-9_]`.
- The room and radiator PUT handlers copy the old MQTT name before updating. On a change, they remove the old device and then announce under the new name. DELETE removes using the MQTT name.
- Renaming loses that device's HA history; this was discussed and accepted.

**Tests:** `tsc`, eslint and the full `idf.py build` (including the vite build) pass. Not flashed or checked against HA. Devices announced under TASK-16's id-based keys will be left in HA if that firmware ever ran; they can be cleared by publishing an empty retained payload to their config topics.
<!-- SECTION:FINAL_SUMMARY:END -->
