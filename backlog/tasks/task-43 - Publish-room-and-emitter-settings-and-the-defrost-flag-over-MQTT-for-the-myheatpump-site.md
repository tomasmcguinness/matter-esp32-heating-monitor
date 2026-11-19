---
id: TASK-43
title: >-
  Publish room and emitter settings and the defrost flag over MQTT for the
  myheatpump site
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-16 15:53'
updated_date: '2026-11-16 15:57'
labels:
  - mqtt
  - firmware
dependencies: []
references:
  - firmware/main/managers/calculations_manager.cpp
  - firmware/main/mqtt/ha_discovery.cpp
  - firmware/main/mqtt/mqtt_publisher.h
  - firmware/main/storage/history_format.h
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A separate self-hosted site (new repo `myheatpump`: ASP.NET Core + React, SQLite, behind Caddy at myheatpump.tomasmcguinness.com) will subscribe to `heating_monitor/#` and store the board's measurements. Today's state topics can't be interpreted on their own:

- Room and radiator topics are keyed by `mqtt_name` only. Nothing on MQTT gives the numeric ids, which are how the SD card names its files (`room-<id>/`, `rad-<id>/`). The site also needs targets, rated outputs, heat loss per °C, radiator membership and UFH shares to derive radiator and UFH output itself.
- `heating_monitor/home` has no defrost flag, though the SD home record has carried one since version 3.

Add a retained `heating_monitor/config` message describing every room and emitter, republished whenever any of them changes, and add `defrosting` to the home payload. Proposed payload shape (`schema` lets the site detect future changes):

```json
{"schema":1,"device_id":"heating_monitor_<mac>",
 "rooms":[{"id":10,"name":"Living Room","mqtt_name":"living_room","target_c":21.0,
           "heat_loss_w_per_c":24,"radiator_ids":[12],"ufh_shares":[{"manifold_id":14,"pct":75}]}],
 "emitters":[{"id":12,"name":"Living Room Radiator","mqtt_name":"living_room_radiator",
              "type":"radiator","rated_w":3464}]}
```

`type` is `radiator` or `ufh`. Every existing state topic and field stays exactly as it is, because Home Assistant discovery depends on them.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 A retained heating_monitor/config message lists every room (id, name, mqtt_name, target, heat loss per degree, radiator ids, UFH shares) and every emitter (id, name, mqtt_name, type radiator or ufh, rated output)
- [ ] #2 The config message is published when the MQTT client connects, so a subscriber that connects later receives it straight away
- [x] #3 Adding, editing or removing a room or radiator, or changing a room's UFH shares, republishes the config message
- [x] #4 A room or emitter with no MQTT name still appears in the config message, with mqtt_name null
- [x] #5 Publishing the config never blocks the CHIP task; it goes through the MQTT publisher task
- [x] #6 heating_monitor/home carries defrosting as true, false or null (null when no defrost source is known), and Home Assistant discovery announces it
- [x] #7 No existing state topic, field name or discovery entity changes
- [x] #8 The firmware builds, and the topics are documented where the existing MQTT topics are documented
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved as Part 1 of the myheatpump plan (~/.claude/plans/let-s-do-our-own-unified-kernighan.md).

1. New module main/mqtt/config_topic.{h,cpp}:
   - config_topic_mark_pending(): sets an atomic flag. Safe from any task.
   - config_topic_publish_pending(client, room_manager, radiator_manager): if the flag was set, builds the payload with cJSON and queues it retained with mqtt_publisher_publish().
2. The payload is {schema:1, device_id, rooms:[...], emitters:[...]}.
   - Each entry carries its state_topic, built with ha_room_state_topic / ha_radiator_state_topic, so the site never re-implements topic naming.
   - mqtt_name is null when empty.
   - Emitter type is "ufh" when is_ufh_manifold(), otherwise "radiator".
   - device_id is exposed from ha_discovery (ha_device_id()).
3. Call config_topic_publish_pending() next to ha_discovery_announce_pending() in update_home(), the one task that may walk the room and radiator lists.
4. Mark pending:
   - in ha_discovery_announce_all(), i.e. on every connect
   - in the app_main HTTP handlers after a room or radiator is added, updated or removed (UFH shares are set in the room add and update handlers)
   - in the home-wide reannounce path
5. Add defrosting (bool/null) to publish_home_state, and a "defrosting" binary sensor to HOME_SENSORS (device_class "cold"? no: use NULL device class with binary true, named "Defrosting").
6. Document heating_monitor/config and defrosting in firmware/CLAUDE.md, next to the existing topic notes (~line 726).
7. Build with idf.py. See the build-environment memory: source IDF v5.5.4 and esp-matter export.sh.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-16: Implemented and building (idf.py build, after reconfigure for the new file). New module mqtt/config_topic.{h,cpp}; each room and emitter entry also carries state_topic so subscribers never rebuild topic names. ha_discovery_device_id() exposes the device id. defrosting is on the home payload and in HOME_SENSORS as a binary sensor (no device class). Pending is marked on connect (ha_discovery_announce_all), in the radiator POST/PUT/DELETE and room POST/PUT/DELETE handlers, and /api/reset; the config is published from update_home(), so it can lag a change by up to a minute.

Not yet verified on hardware: AC #2 (retained config after connect, replayed to a late subscriber). Needs a flash and `mosquitto_sub -v -t 'heating_monitor/#'`.

These changes sit on feature/ufh-emitters alongside the uncommitted TASK-42 UFH work, which the config payload depends on (ufh_shares, is_ufh_manifold).
<!-- SECTION:NOTES:END -->
