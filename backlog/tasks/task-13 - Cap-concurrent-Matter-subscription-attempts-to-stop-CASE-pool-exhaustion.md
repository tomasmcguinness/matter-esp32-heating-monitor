---
id: TASK-13
title: Cap concurrent Matter subscription attempts to stop CASE pool exhaustion
status: In Progress
assignee: []
created_date: '2026-09-26 10:27'
updated_date: '2026-09-26 10:27'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Subscriptions failed with CHIP_ERROR_NO_MEMORY (0x0B) at OperationalSessionSetup.cpp:254 because the controller's CASE client pool was full. The subscription worker started an attempt every second without waiting for earlier ones to resolve, so attempts at unreachable Thread nodes piled up.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 At most MAX_ATTEMPTS_IN_FLIGHT (4) subscription attempts are in flight at once; the worker waits for a free slot
- [ ] #2 A slot is freed when an attempt is established, fails to connect, terminates before establishing, or fails to send/schedule
- [ ] #3 A slot not reported back within 90 s is reclaimed and the node's pending flag cleared
- [ ] #4 No CHIP_ERROR_NO_MEMORY at OperationalSessionSetup.cpp:254 on the board under normal operation
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented in subscription_manager.cpp:
- a slot table with a mutex
- claim_slot() in the worker, called before ScheduleWork
- subscription_attempt_finished(), called from the established, terminated and failed callbacks in app_main.cpp and from every early return in send_subscription

The 90 s timeout covers the one esp-matter path that fires no callback (session connects, then send_request fails). A ScheduleWork failure now frees its slot and clears the pending flag. Firmware builds. Not verified on hardware.

Still open: a failed node is re-queued immediately (paced at 1 s), so it keeps retrying; a backoff would reduce that. Reads and ICD check-ins aren't capped.
<!-- SECTION:NOTES:END -->
