---
id: TASK-14
title: Treat null or implausible TemperatureMeasurement readings as no reading
status: Done
assignee: []
created_date: '2026-10-01 05:35'
updated_date: '2026-10-01 05:37'
labels:
  - firmware
  - bug
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Radiators 9 and 12 never report an output. Radiator 12's flow probe reports MeasuredValue null; app_main decodes the nullable attribute into a plain int16_t, ignores the failed decode and uses an uninitialised value. Radiator 9's flow probe reports about -80°C (disconnected sensor) most of the time. Decode MeasuredValue as Nullable<int16_t>, check the decode result, and treat null or physically implausible values as "no reading" so a bad probe cannot feed the calculations.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 MeasuredValue is decoded as a Nullable<int16_t> and a failed decode is logged and ignored
- [x] #2 A null MeasuredValue is stored as no reading (0) for the endpoint, radiator, room and outdoor sensor
- [x] #3 Values outside a plausible range are logged and treated as no reading
- [x] #4 Firmware builds
<!-- AC:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
MeasuredValue is now decoded as Nullable<int16_t> in app_main's attribute_data_cb; a failed decode is logged and the report ignored. Null readings, and readings outside -50°C..100°C (MIN/MAX_PLAUSIBLE_TEMPERATURE), are logged and stored as 0, the existing "no reading" sentinel, so the endpoint, radiator and room paths skip them. The outdoor sensor sets has_outdoor_temperature from whether the reading was valid. Build succeeds; not yet flashed or verified on hardware.

Not changed: update_radiator_outputs returns early on a missing reading without clearing heat_output, so a radiator whose probe fails after a good reading keeps its last output.
<!-- SECTION:FINAL_SUMMARY:END -->
