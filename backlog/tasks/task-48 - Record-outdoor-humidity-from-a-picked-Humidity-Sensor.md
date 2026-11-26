---
id: TASK-48
title: Record outdoor humidity from a picked Humidity Sensor
status: In Progress
assignee: []
created_date: '2026-11-26 08:23'
updated_date: '2026-11-26 08:28'
labels:
  - firmware
  - web-ui
  - history
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Add an Outdoor Humidity Sensor pick to Edit Home, subscribe RelativeHumidityMeasurement on that node only, expose it on /api/home and Home, and log it in history like the outdoor temperature.

2026-11-26: Implemented, built (IDF v5.5.4) and flashed. Host tests pass. Older NVS blob loads with every existing pick intact, and /api/history names field 12 outdoorHumidityC100. NOT verified end to end: the Matterbridge (node 0x272f) exposes no Humidity Sensor endpoint, only 264 Outdoor Temperature (770), so nothing has been picked yet. The humidity entity has to be added to Matterbridge first, and the node probably needs an Interview to see the new endpoint. Not on MQTT, emoncms, MyHeatPump or MCP.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Edit Home has an Outdoor Humidity Sensor picker (device type 775), persisted at the end of the home NVS blob
- [ ] #2 Only the picked node is subscribed to humidity; changing the pick re-subscribes the old and new nodes
- [x] #3 GET /api/home carries outdoorHumidity (null until reported) and the sensor's node, endpoint and subscription state
- [x] #4 History records it in rec_home_t field 12 (was reserved1) with no record size or version change, and History charts it
- [ ] #5 A reading is verified end to end from a real sensor
- [x] #6 MyHeatPump status carries system.outdoor_humidity_pct and MCP get_series has an outdoor_humidity_pct series
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-26: Added to MyHeatPump (system.outdoor_humidity_pct, null when absent) and MCP (outdoor_humidity_pct series from field 12; get_series units note; server instructions). test_server now checks 13 series and that the humidity value survives history_convert (184 checks, 0 failed). Flashed; the live list_series shows the new series. MyHeatPump post not observed: it stays null until a sensor is picked.
<!-- SECTION:NOTES:END -->
