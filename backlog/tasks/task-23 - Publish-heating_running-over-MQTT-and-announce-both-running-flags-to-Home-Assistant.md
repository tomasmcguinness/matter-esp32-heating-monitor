---
id: TASK-23
title: >-
  Publish heating_running over MQTT and announce both running flags to Home
  Assistant
status: In Progress
assignee: []
created_date: '2026-10-10 07:02'
updated_date: '2026-10-10 07:03'
labels: []
dependencies:
  - TASK-20
references:
  - firmware/main/managers/calculations_manager.cpp
  - firmware/main/mqtt/ha_discovery.cpp
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The MQTT home state carried dhw_running but not heating_running, and neither was in HA discovery. Publish heating_running (true/false/null) on heating_monitor/home and announce heating_running and dhw_running as binary sensors.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 heating_monitor/home carries heating_running as true, false or null
- [ ] #2 The home device's discovery config announces heating_running and dhw_running as binary_sensor components with device class running
- [ ] #3 With no heat pump picked both entities read unknown in Home Assistant
- [ ] #4 Existing sensor components are unchanged
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented and built (2026-10-10); not flashed, so no criterion is verified on the board or in Home Assistant yet. update_home() adds heating_running beside dhw_running. ha_sensor_t gained a trailing `binary` flag; build_component() emits binary_sensor, no state_class, and a template giving ON/OFF or None for null. The None-for-unknown payload is from memory of HA's MQTT binary_sensor behaviour and needs checking against the HA log after flashing.

MQTT is published on the next update_home(), not the moment the running state changes (a heat pump report only calls broadcast_home_state()).
<!-- SECTION:NOTES:END -->
