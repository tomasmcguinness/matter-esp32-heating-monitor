---
id: TASK-34
title: Stop the CHIP event queue filling and aborting the monitor
status: In Progress
assignee: []
created_date: '2026-11-01 16:50'
labels:
  - firmware
  - bug
dependencies: []
priority: high
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The monitor panics when the CHIP platform event queue fills: lwIP's task fails to post an incoming UDP packet, logs "Failed to post event to CHIP Platform event queue", and CHIP aborts on purpose. On 2026-11-01 it restarted at 16:28:49, 16:30:03 and 16:32:00 with the same backtrace, and the board restarted 18 times that day, so sensors, history and pairing are all interrupted. The first crash was during a burst of heat pump attribute reports, the other two while nodes were resubscribing after boot.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The monitor stays up with no event queue abort through a restart and the full resubscription of every node
- [ ] #2 The monitor stays up with no event queue abort for at least 30 minutes of normal reports from all nodes
- [ ] #3 A burst of attribute reports no longer does a full recalculation, JSON build and MQTT publish per attribute on the CHIP task
- [ ] #4 A slow or unreachable MQTT broker cannot stall the CHIP task
- [ ] #5 Home, room and radiator state still reaches MQTT, the WebSocket and history with its latest values
- [ ] #6 sdkconfig and sdkconfig.defaults agree on the event queue size
- [ ] #7 CLAUDE.md describes the new behaviour
<!-- AC:END -->
