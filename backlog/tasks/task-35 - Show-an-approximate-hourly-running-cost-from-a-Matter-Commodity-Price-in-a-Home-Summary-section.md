---
id: TASK-35
title: >-
  Show an approximate hourly running cost from a Matter Commodity Price, in a
  Home Summary section
status: In Progress
assignee: []
created_date: '2026-11-01 17:45'
updated_date: '2026-11-01 17:57'
labels:
  - firmware
  - web-app
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The monitor knows what the heat pump draws but not what that costs. Matter's Commodity Price cluster (0x0095), served by the Electrical Energy Tariff device type (1299), carries the live tariff. Let the user pick a Commodity Price on Edit Home, and replace the Home screen's Indoors and Outdoors sections with one Summary section. The running cost is current electrical power multiplied by current price, so it moves with compressor cycles.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Edit Home offers a Commodity Price picker listing Electrical Energy Tariff endpoints, and the choice persists across a restart
- [x] #2 A Home blob saved by older firmware still loads, with no price picked
- [ ] #3 Home shows one Summary section with Average Indoor Temperature, Outdoor Temperature, COP and Approx Hourly Running Cost, and no Indoors, Outdoors or Forecast sections
- [ ] #4 The hourly cost equals electrical power multiplied by the current price, scaled by the currency's decimal points
- [ ] #5 The cost shows a dash when power or price is missing, and offers to configure a price when none is picked
- [ ] #6 A price picked after its device subscribed shows without waiting for the price to change
- [ ] #7 A price stops being shown when its device's subscription ends
- [x] #8 CLAUDE.md describes the behaviour
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Built and flashed on 2026-11-01 without erasing NVS. Node list and all 10 saved Home picks and intervals were unchanged across the upgrade (AC 2), and /api/home reports commodityPrice null with node 0. The served app contains the Summary and no longer contains Indoors.

Not yet tested against a real price: no Electrical Energy Tariff device is paired to this board, so the attribute decode, the picker listing a device, persistence of a pick, the cost figure on real data and the forget path are unexercised. Pair matter-esp32-electrical-energy-tariff-device and run verification steps 4 and 5 in the plan before closing. The Summary page also has not been looked at in a browser.
<!-- SECTION:NOTES:END -->
