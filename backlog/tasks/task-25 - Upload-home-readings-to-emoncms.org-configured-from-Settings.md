---
id: TASK-25
title: 'Upload home readings to emoncms.org, configured from Settings'
status: In Progress
assignee: []
created_date: '2026-10-11 07:53'
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
- [ ] #1 idf.py build succeeds, including the React app
- [ ] #2 Only readings whose has_* flag is set are sent; a post with no values is skipped
- [ ] #3 Unit conversions reuse the ones the web UI, history logger and MQTT already apply, in one table
- [ ] #4 GET /api/emoncms/config never returns the API key, only key_set
- [ ] #5 PUT /api/emoncms/config applies without a reboot and an empty key keeps the existing one
- [ ] #6 Failed posts are buffered in PSRAM (about an hour), retried oldest first with original timestamps, oldest dropped when full
- [ ] #7 Settings page has an emoncms card with config, Save, Send test and a status area showing the last payload as a key/value table
- [ ] #8 MQTT URL, username and password come from NVS and are no longer in the source
- [ ] #9 No API key or password appears in logs, source or any GET response
<!-- AC:END -->
