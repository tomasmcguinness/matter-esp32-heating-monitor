---
id: TASK-24
title: Show the day's heat split into heating and hot water on the History page
status: In Progress
assignee: []
created_date: '2026-10-10 15:49'
labels: []
dependencies: []
references:
  - firmware/html_app/src/History.tsx
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The History page's first card row showed combined Heat out, Electricity in and Sampled every. Replace it with Heating (kWh), Hot water (kWh) and Electricity in, and remove the Sampled every card. Follows on from TASK-21, which added the per-mode energy to the API (the dependency link was rejected by the tool as "does not exist", so it is recorded here instead).
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The first card row is Heating, Hot water and Electricity in
- [ ] #2 Heating and Hot water show that mode's heat in kWh, matching modes.*.heatWh from /api/history
- [ ] #3 A mode card shows a dash when its COP card does
- [ ] #4 The Sampled every card is gone
<!-- AC:END -->
