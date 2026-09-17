#pragma once

#include "mqtt_client.h"

// Home Assistant MQTT discovery for the Heating Monitor itself.
//
// The home's readings are already published to heating_monitor/home by update_home(); this tells
// Home Assistant that topic exists and how to read each value out of it. Without a discovery
// message HA ignores the topic entirely, which is why none of this data has been reaching it.
//
// One device carries every entity. The id is derived from the Ethernet MAC, so radiators and
// rooms can later be announced against the same `ids` and group under it rather than each
// inventing a device of its own.

// Retained availability topic, backed by the client's last will: "online" while connected,
// "offline" the moment the broker notices the device has gone.
#define HA_AVAILABILITY_TOPIC "heating_monitor/status"

#ifdef __cplusplus
extern "C" {
#endif

// Publishes the retained discovery config. Safe to call on every reconnect -- the config topic is
// keyed on the device id, so a republish replaces rather than duplicates.
//
// Publishes at QoS 0: esp-mqtt can deadlock if a publish at QoS > 0 is issued from inside the
// MQTT event handler, which is where this is called from.
void ha_discovery_announce_all(esp_mqtt_client_handle_t client);

// Publishes the retained "online" availability message. Call once the broker connection is up.
void ha_discovery_publish_online(esp_mqtt_client_handle_t client);

#ifdef __cplusplus
}
#endif
