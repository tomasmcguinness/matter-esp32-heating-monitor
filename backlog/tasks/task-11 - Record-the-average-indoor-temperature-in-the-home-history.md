---
id: TASK-11
title: Record the average indoor temperature in the home history
status: In Progress
assignee: []
created_date: '2026-09-26 05:27'
updated_date: '2026-09-26 05:34'
labels: []
dependencies: []
references:
  - firmware/main/managers/calculations_manager.cpp
  - firmware/main/mqtt/ha_discovery.cpp
  - firmware/main/storage/history_logger.cpp
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The home history record's internal_temp_c100 column was never filled because nothing set has_internal_temperature. Derive it in update_home() as the simple mean of rooms with a live temperature reading, rename the field to average_internal_temperature, and announce it to Home Assistant.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 update_home() sets has_average_internal_temperature/average_internal_temperature to the simple mean of rooms with a bound sensor and a non-zero reading; absent when there are none
- [ ] #2 The history home record's average_internal_temp_c100 column is populated from it, exposed by the history API as averageInternalTempC100 and charted as "Average Indoor"
- [ ] #3 MQTT heating_monitor/home publishes average_internal_temperature and HA discovery announces it
- [ ] #4 GET /api/home averageInternalTemperature reflects the average
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented in update_home() (calculations_manager.cpp): simple mean over rooms with room_temperature_node_id != 0 and current_temperature != 0. It is named "average internal temperature" everywhere:
- home_manager.h: average_internal_temperature / has_average_internal_temperature
- on-disk record: rec_home_t.average_internal_temp_c100 (rename only; same position and size, so existing files stay readable)
- MQTT key average_internal_temperature, plus the HA discovery row "Average indoor temperature"
- GET /api/home: averageInternalTemperature
- history API column: averageInternalTempC100 (history_api.c, History.tsx FIELDS/COL)
- History chart series label: "Average Indoor"

Firmware and web app build. Not yet verified on hardware.
<!-- SECTION:NOTES:END -->
