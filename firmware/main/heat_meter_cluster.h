#pragma once

#include <stdint.h>

// Manufacturer-specific "Heat Meter" cluster, as published by the M-Bus adapter. It carries the
// full meter dataset at higher precision than the standard Flow Measurement cluster (which is only
// a uint16 in 0.1 m^3/h).
//
// This is a copy of matter-esp32-mbus-adapter's firmware/main/heat_meter_cluster.h -- the two must
// be kept in sync. 0xFFF1 is a Matter *test* vendor id; if the adapter is ever given an allocated
// Vendor ID, both copies have to change together.
//
#define HEAT_METER_VENDOR_PREFIX     0xFFF1u
#define HEAT_METER_DEVICE_TYPE_ID    0xFFF10001u
#define HEAT_METER_DEVICE_TYPE_VER   1
#define HEAT_METER_CLUSTER_ID        0xFFF1FC01u

// Attribute IDs within the Heat Meter cluster.
#define HM_ATTR_FLOW_ID            0x0000u  // float,  m^3/h
#define HM_ATTR_FLOW_TEMP_ID       0x0001u  // int32,  0.01 degC
#define HM_ATTR_RETURN_TEMP_ID     0x0002u  // int32,  0.01 degC
#define HM_ATTR_POWER_ID           0x0003u  // int64,  mW
