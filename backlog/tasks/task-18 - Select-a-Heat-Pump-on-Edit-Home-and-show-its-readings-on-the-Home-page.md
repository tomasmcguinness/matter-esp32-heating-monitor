---
id: TASK-18
title: Select a Heat Pump on Edit Home and show its readings on the Home page
status: Done
assignee: []
created_date: '2026-10-04 12:37'
updated_date: '2026-10-04 12:44'
labels: []
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The monitor recognises the Matter Heat Pump device type (0x0309 / 777) only as a label in the Devices list. The user wants to pick a heat pump on the Edit Home page and, once one is picked, see a read-only Heat Pump section showing what the device exposes.

Reference device: the Kronoterm Adapt example in ~/development/matter-esp32-modbus-adapter/examples/matter/KronotermAdapt (see its README). It exposes: EP1 Heat Pump + Electrical Sensor (ActivePower only), EP2 Thermostat (heating loop 1: LocalTemperature, OccupiedHeatingSetpoint, SystemMode, ThermostatRunningState), EP3-5 Temperature Sensors (outdoor, flow, return; identified only by Descriptor TagList labels, no FixedLabel), EP6 Water Heater (Thermostat cluster plus WaterHeaterManagement HeatDemand/BoostState and WaterHeaterMode CurrentMode). Everything is read-only; the monitor must not write to the device.

Out of scope: history logging, MQTT / Home Assistant announcement, feeding COP from the heat pump's power, and the companion API.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Edit Home offers a Heat Pump picker listing endpoints with the Heat Pump device type, and the choice persists across a restart
- [x] #2 With no heat pump selected the Home page shows no Heat Pump section
- [x] #3 With a heat pump selected the Home page shows a Heat Pump section with its subscription state and electrical power
- [x] #4 The section shows each temperature sensor on the heat pump node, named from the device's semantic tag label where it has one
- [x] #5 The section shows each thermostat and water heater on the node: current temperature, heating setpoint, mode and running state, plus boost and heat demand for a water heater
- [x] #6 Values update live over the home WebSocket channel and a value the device has not reported renders as a dash, not zero
- [x] #7 Nothing in the section is editable and the firmware sends no writes or commands to the heat pump
- [x] #8 firmware/CLAUDE.md documents the heat pump selection and data flow
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. home_manager: persist heat_pump_node_id/endpoint_id, appended to the NVS blob with a size check on load.
2. heat_pump_manager: transient table of per-endpoint readings (power, Thermostat, WaterHeaterManagement) keyed on node + endpoint.
3. subscription_manager: nodes with the Heat Pump device type also subscribe to Thermostat (LocalTemperature, OccupiedHeatingSetpoint, SystemMode, ThermostatRunningState) and WaterHeaterManagement (HeatDemand, BoostState).
4. app_main: store those reports and the node's ActivePower; read Descriptor TagList in the node interview to name unlabelled endpoints; add heatPump* fields to GET/PUT /api/home and the home WebSocket push; drop a node's readings when its subscription ends or it is unpaired.
5. Web UI: Heat Pump picker on Edit Home; HeatPump.tsx section on Home, shown only once a heat pump is picked.
6. Document in firmware/CLAUDE.md.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
- Readings are cached for every heat pump node, not only the selected one: a subscription only re-reports on change, so caching after selection would leave the section dashed.
- The section is built from all endpoints on the heat pump's node; the heat pump endpoint's PartsList is not read.
- WaterHeaterMode CurrentMode is not shown: mode values are device-defined and labelling needs SupportedModes.
- Tag labels are read at commissioning only, so a node paired on older firmware shows "Endpoint N" until re-paired.
- heatPumpNodeId/heatPumpEndpointId are optional on PUT /api/home, so an older client does not clear the selection.
- Verified by `tsc -b` and a full `idf.py build` only. Not flashed or run against a Kronoterm adapter, so acceptance criteria are checked against the code, not observed on hardware.

Follow-up at the user's request: WaterHeaterMode SupportedModes and CurrentMode are now subscribed, and the hot water zone gains an Operation card showing the device's own label for the current mode ("Mode N" if the mode is not in SupportedModes). This supersedes the earlier note that CurrentMode is not shown. A heat pump node's subscription is now up to 12 attribute paths. Builds; still not run on hardware.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Adds read-only heat pump support to the Home page.

What changed
- Edit Home: new Heat Pump picker (device type 777). The selection is stored in the home_manager NVS blob (appended; older blobs still load).
- Home: a Heat Pump section appears once one is selected, with a subscription pill, the heat pump's electrical power, a card per temperature sensor on the node, and a zone per Thermostat / Water Heater (temperature, target, mode, state; boost and heat demand for a water heater). Updates arrive on the existing home WebSocket channel; unreported values render as a dash.
- Firmware: new transient heat_pump_manager; heat pump nodes additionally subscribe to Thermostat and WaterHeaterManagement attributes; attribute_data_cb stores them along with ActivePower; GET /api/home gains heatPumpNodeId, heatPumpEndpointId, heatPumpSubscription and a heatPump object; PUT /api/home accepts the two ids.
- Node interview now reads Descriptor TagList and uses the first tag label as the endpoint name when the endpoint has none, which is how the Kronoterm's Outdoor / Flow / Return sensors are told apart.
- firmware/CLAUDE.md documents the feature.

Not included: history logging, MQTT / Home Assistant, COP from the heat pump's power, companion API, WaterHeaterMode CurrentMode.

Testing: `tsc -b` clean and `idf.py build` links. Not flashed; untested against a real or simulated Kronoterm adapter.

Risks / follow-ups
- A heat pump node now subscribes with up to 10 attribute paths (1 temperature + 3 electrical + 6 heat pump). Should be within the device's path pool but is unverified on hardware.
- A Kronoterm paired before this firmware needs re-pairing to pick up tag labels.
- Devices.tsx still shows the Water Heater device type as "1295".

Update: the hot water zone also shows the Water Heater Mode, labelled from the device's SupportedModes (subscribed alongside CurrentMode). WaterHeaterMode CurrentMode is therefore no longer in the 'Not included' list, and the subscription is up to 12 attribute paths rather than 10.
<!-- SECTION:FINAL_SUMMARY:END -->
