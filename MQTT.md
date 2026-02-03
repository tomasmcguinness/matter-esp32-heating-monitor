```
{
  "dev": {
    "ids": "ea334450945afc",
    "name": "Office Radiator",
    "mf": "Cold Bear"
  },
  "o": {
    "name":"HeatingMonitor",
    "sw": "1.0",
    "url": "https://tomasmcguinness.com/support"
  },
  "cmps": {
    "flow_temperature": {
      "p": "sensor",
      "device_class":"temperature",
      "unit_of_measurement":"°C",
      "value_template":"{{ value_json.temperature}}",
      "unique_id":"office_radiator_flow_temperature",
      "default_entity_id":"sensor.office_radiator_flow_temperature"
    },
    "return_temperature": {
      "p": "sensor",
      "device_class":"temperature",
      "unit_of_measurement":"°C",
      "value_template":"{{ value_json.humidity}}",
      "unique_id":"office_radiator_return_temperature",
      "default_entity_id":"sensor.office_radiator_return_temperature"
    }
  },
  "state_topic":"radiators/office/state",
  "qos": 2
}
```