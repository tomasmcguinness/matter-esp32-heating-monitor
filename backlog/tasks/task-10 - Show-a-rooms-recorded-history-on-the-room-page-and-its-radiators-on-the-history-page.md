---
id: TASK-10
title: >-
  Show a room's recorded history on the room page and its radiators on the
  history page
status: Done
assignee: []
created_date: '2026-09-24 15:42'
updated_date: '2026-09-25 05:27'
labels:
  - web-ui
  - history
dependencies: []
priority: medium
ordinal: 7000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A room's page shows only live values, so nothing on it says how the room has actually behaved today — the recorded history sits behind a button in the heading. And the room history page charts the room's air temperature alone, even though the radiators feeding it are recorded on the same slot clock and are what explains the shape of the room's line.

Two changes: today's room temperature charted on the room page, clickable through to the history page; and one flow/return chart per radiator on the room history page, below the room's own chart.

No firmware change is needed. `GET /api/history/radiator?id=N[&date=][&points=]` is already implemented and returns `flowTempC100`/`returnTempC100`, but no UI has ever called it — this is its first consumer. `GET /api/rooms/:roomId` already carries the room's radiators inline, so enumerating them needs no extra request.

Design decisions taken with the user: the room page shows room temperature only, kept compact; the history page shows the room chart followed by one chart per radiator rather than a single combined chart, so a radiator's 10 degC swing isn't flattened onto the room's narrow scale.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 The room page shows a chart of the current day's recorded room temperature, using the device's own local day rather than the browser's
- [x] #2 Clicking or keyboard-activating that chart opens the room's history page
- [x] #3 The room page's chart degrades quietly when there is no SD card, no readings yet, or the clock has not synced — a muted line, not a page-sized alert or an error
- [x] #4 The room history page shows one chart per radiator in that room, below the room's chart, each titled with the radiator's name and carrying flow and return traces
- [x] #5 Changing the date reloads every chart, and clicking quickly through dates never leaves a chart showing another day's data
- [x] #6 A radiator with no recorded data for the chosen day reads as no readings, not as an error, including when the response carries no fields key
- [x] #7 Radiator series are requested one after another rather than all at once, so a room with several radiators cannot exhaust the device's socket table
- [x] #8 A room with no radiators renders the room chart alone
- [x] #9 The field-name check is shared rather than hand-rolled a third time, and tolerates a field being added to the record's reserved tail
- [x] #10 Every chart is wrapped in a card with its title as the header, including its empty and error states, so a page of charts reads as a stack of panels and does not shift when one has nothing to draw
- [x] #11 The radiator chart carries derived heat output in watts on a second y axis alongside flow and return, matching the arithmetic update_radiator_outputs() uses for the live figure
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Web-app only; no firmware change was needed. `tsc -b` and the firmware build are both clean (`heating-monitor.bin` 0x229680 bytes, 57% of the app partition free). Not flashed — the on-device checks in the plan's verification section are outstanding.

**`historyData.ts`** — added `ROOM_CHART_FIELDS`, `RADIATOR_CHART_FIELDS` and `fieldsMatch(body, expected)`, replacing the hand-rolled `body.fields.join(",") !== FIELDS.join(",")` that was about to be copied a third time.

`fieldsMatch` compares only the **leading** columns the caller charts, which is a deliberate behaviour change rather than a tidy-up: `history_format.h` reserves a tail on every record so a quantity can be added later, and `history_api.c` names its columns precisely so "a field added to the reserved tail shows up as a named column without the UI needing to be rebuilt". The exact join-compare rejected exactly that case. It also tolerates a response with no `fields` at all, which is how the firmware answers a day it holds no file for.

`History.tsx` still has its own exact check — left alone deliberately, noted as a follow-up.

**New `RoomTodayChart.tsx`** — self-contained: fetches, renders and handles its own empty states so `Room.tsx` stays a layout. Requests `/api/history/room?id=N&points=200` with **no `date`**, because omitted means the device's own local day, which is what the files on the card are named after; sending the browser's date would fetch the wrong day whenever a phone and the board disagree about midnight. 200 points rather than 800 — it's a glance, read off an SD card by a single-task server.

Wrapped in a `role="link"` / `tabIndex` / Enter-and-Space container navigating to the room's history page. uPlot's cursor listeners sit on `u.over` and don't stop propagation, so no uPlot hook was needed. Every non-chart state is a muted line rather than an alert, since this is secondary content on a page whose live values already work — including the `400` a dateless request gives until the first SNTP sync, which reads as "Nothing has been recorded yet."

**`Room.tsx`** — one new card, "Temperature today", between the summary cards and the Radiators table. Passes `room.roomId` from the payload rather than the route param, avoiding a non-null assertion.

**`RoomHistory.tsx`** — the existing `GET /api/rooms/:id` call already carried the radiators inline and was discarding everything but `name`; it now keeps them, so there is no extra request and no second source of truth for which radiators are in the room.

Radiator series load **one at a time** in an effect keyed on `[id, date]`, appending as each lands so charts fill in progressively. Sequential because the device serves HTTP from a single task with a small socket table and `lru_purge_enable` on — several radiators streaming 800 points at once can evict the web UI's WebSocket. A `cancelled` flag bails rather than appends when the date moves on mid-sequence; the page previously had one only for its name fetch.

Each radiator renders through a `RadiatorChart` child so its plot data can be memoised per radiator (a `useMemo` can't live in a loop in the parent), and so a future `/radiators/:radiatorId/history` page has something to reuse — `Radiator.tsx` is currently a stub. Flow `#d9534f` / return `#5bc0de`, the colours `History.tsx` already uses.

The radiator section sits **outside** the `hasPoints` block: a room with no temperature sensor bound still has radiators worth charting. It is suppressed when the card is missing, so one absent card is one message rather than one per radiator.

**No index-joining anywhere.** Separate charts mean each series draws against its own column-0 timestamps, which sidesteps the real trap: `stride` is computed per file from that file's `total_slots`, so a radiator whose file was created mid-day has a different slot count from the room's and would not align by array index.

**Adjacent issues found, deliberately not fixed:** `Room.tsx` has no error state, so a failed `GET /api/rooms/:id` leaves it on "Loading…" forever; `room_get_handler` and `radiator_get_handler` dereference the result without checking the lookup succeeded, making an unknown id a null deref rather than a 404; `History.tsx` recomputes `toPlotData` every render against its own doc comment, rebuilding all three plots.

Follow-up in the same session: box all the charts, and add Output to the radiator chart.

**Boxing** went into `HistoryChart.tsx` rather than each call site. New exported `ChartCard` (card + header + body), and `Chart` now renders itself inside one, so `History.tsx`'s three charts and the room history charts are boxed without touching either page. uPlot is no longer given a `title` — the card header carries it, and uPlot would otherwise draw a second. `Room.tsx`'s hand-rolled card came back out, and the title moved into `RoomTodayChart` so the chart owns its own panel.

`ChartCard` is exported because the empty and error states need the same box: without it the panel vanishes when there is nothing to draw and the page shifts under whatever is below. `RoomTodayChart`'s four muted states and `RadiatorChart`'s two both render one now.

**Output** could not simply be charted — it is not a recorded column. `rec_radiator_t` stores flow and return only, deliberately, because output is a function of those plus NVS configuration and a stored copy would freeze whatever the arithmetic was on the day. `firmware/CLAUDE.md` says the deriving belongs off the device, so it is computed per point in the browser with the same arithmetic as `update_radiator_outputs()` (`calculations_manager.cpp:39-68`): `mwt = (flow+return)/2`, `ΔT = |mwt - room air|`, `output = rated / (50/ΔT)^1.3`. The firmware's guards are reproduced — a non-positive flow, return or room reading is a gap rather than a zero — and ΔT of 0 yields 0 W, which is what the firmware's division by ΔT produces. Unlike the firmware it is not truncated to a uint16.

Two inputs were missing and neither needed a firmware change: the rated output at dT 50 is not in the room payload, so the page fetches `GET /api/radiators` once for every radiator's `output`; and the room's air temperature comes from the room's own series, which now doubles as a timestamp lookup. **Joined on the timestamp, not the array index** — the three kinds share one sampling interval, but `stride` is computed per file from that file's slot count, so a radiator whose file started mid-day buckets differently from the room's.

Watts cannot share the °C axis, so `ChartProps` gained `rightUnit`/`rightDecimals` and `ChartSeries` gained `axis: "right"`; such a series gets `scale: "y2"` and a second axis on `side: 1` with its grid off. Without a rating the right axis is omitted entirely rather than labelled against an empty line. Mixing units on one axis is still impossible, so `History.tsx` keeps one chart per unit.

`firmware/CLAUDE.md`'s web-app section now records the ChartCard contract, the second-axis rule, and the recorded-vs-derived split.
<!-- SECTION:NOTES:END -->
