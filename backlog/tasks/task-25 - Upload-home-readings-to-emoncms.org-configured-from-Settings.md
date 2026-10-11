---
id: TASK-25
title: 'Upload home readings to emoncms.org, configured from Settings'
status: In Progress
assignee: []
created_date: '2026-10-11 07:53'
updated_date: '2026-10-11 08:01'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Post nine home-level readings (flow rate, flow/return/internal/outdoor temperature, electricity in, heat out, heating_running, dhw_running) to an emoncms account over HTTPS from a new main/emoncms/ module running in its own task. Config lives in NVS namespace "emoncms" and is edited from a card on the Settings page; failed posts are held in a bounded PSRAM ring buffer and retried oldest first with their original timestamps. No room or radiator data and no COP: each input is a paid feed.

Follow-on in a separate commit: move the hard-coded MQTT URL, username and password in start_mqtt_service() to NVS with the same secret handling and a matching Settings card.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 idf.py build succeeds, including the React app
- [ ] #2 Only readings whose has_* flag is set are sent; a post with no values is skipped
- [x] #3 Unit conversions reuse the ones the web UI, history logger and MQTT already apply, in one table
- [x] #4 GET /api/emoncms/config never returns the API key, only key_set
- [x] #5 PUT /api/emoncms/config applies without a reboot and an empty key keeps the existing one
- [ ] #6 Failed posts are buffered in PSRAM (about an hour), retried oldest first with original timestamps, oldest dropped when full
- [x] #7 Settings page has an emoncms card with config, Save, Send test and a status area showing the last payload as a key/value table
- [x] #8 MQTT URL, username and password come from NVS and are no longer in the source
- [ ] #9 No API key or password appears in logs, source or any GET response
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented on branch emoncms-uploader in four commits: uploader module, HTTP API, Settings section, MQTT settings in NVS. idf.py build passes, including the React app.

Not yet run on the board. Still to check there: the nine inputs arriving on emoncms.org with correct timestamps and values, the 10 minute Ethernet-unplug backfill (#6), a payload with missing readings (#2), and that no secret reaches the log (#9).

Decisions:
- flow_rate is sent in l/min as heat_meter_flow / 60. The heat meter reports l/h, and nothing else in the code converts to l/min (MQTT and history carry l/h, the web UI shows L/s).
- POST /api/emoncms/test parks the httpd task until emoncms answers (10 s HTTP timeout), like commissioning does.
- The backlog is posted 250 ms apart; emoncms.org's rate limits were not checked against that.
- MQTT URL must be mqtt:// (no mqtts), and removing a username or password takes a restart because esp_mqtt_set_config ignores NULL fields.
- The old MQTT password is still in git history and should be changed at the broker.
<!-- SECTION:NOTES:END -->
