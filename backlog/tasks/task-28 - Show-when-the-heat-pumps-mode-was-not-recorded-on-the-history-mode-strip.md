---
id: TASK-28
title: Show when the heat pump's mode was not recorded on the history mode strip
status: In Progress
assignee: []
created_date: '2026-10-16 18:38'
updated_date: '2026-10-16 18:38'
labels:
  - web-app
dependencies: []
references:
  - firmware/html_app/src/historyData.ts
  - firmware/html_app/src/HistoryChart.tsx
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The mode strip under the history charts draws a slot with no recorded heating/hot water state the same as an idle one (blank), so a lost heat pump subscription looks like the heat pump switching off. On 2026-10-16 the state was null from 18:00 to 19:20 while heat and electrical readings carried on, and the strip showed nothing. Draw those slots as a distinct "No data" segment with its own key entry. A day with no recorded mode at all (before the column existed, or no heat pump picked) still shows no strip.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Slots where both heatingRunning and dhwRunning are null are drawn on the strip as a distinct No data segment, visibly different from idle
- [x] #2 The key lists No data only when the chart shows some
- [x] #3 A day with no recorded mode at all still shows no strip and no key
- [x] #4 Applies to every chart that shows the strip (History, Room History, Room and Radiator today charts)
- [x] #5 tsc passes and eslint is clean on the files touched
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Web app only. modeSpans() in historyData.ts emits a "noData" span for buckets where both heatingRunning and dhwRunning are null, but only on a day where at least one bucket recorded a mode, so old days and days with no heat pump picked still have no strip. HistoryChart.tsx draws it grey (#adb5bd) and adds "No data" to the key only when such a span exists. Every chart that takes `modes` gets it, since they all go through modeSpans(). isHeatingAt() is unchanged: a noData span is not heating, so a radiator's derived output still reads 0 W during an outage rather than a gap. firmware/CLAUDE.md updated.

Verified: tsc -b passes, eslint clean on both files, modeSpans() checked with node against a synthetic day (heating / nulls / heating gives heating, noData, heating; an all-null day gives no spans). Not seen in a browser or on the device yet.
<!-- SECTION:NOTES:END -->
