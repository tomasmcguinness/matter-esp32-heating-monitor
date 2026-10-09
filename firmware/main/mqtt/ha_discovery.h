#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mqtt_client.h"
#include "managers/room_manager.h"
#include "managers/radiator_manager.h"

// Home Assistant MQTT discovery for the Heating Monitor, its rooms and its radiators.
//
// The readings are published by calculations_manager -- the home to heating_monitor/home, each
// room and radiator to its own state topic -- and this tells Home Assistant those topics exist
// and how to read each value out of them. Without a discovery message HA ignores a topic entirely.
//
// The Heating Monitor is one device, its id derived from the Ethernet MAC. Every room and every
// radiator is a device of its own, keyed on that same id plus its MQTT name (or its id, when it
// has none), and linked back to the Heating Monitor with via_device. The MQTT name also sets the
// entity ids. Because the key is a name that can change, renaming one means removing the device
// under the old name and announcing it again under the new one.

// Retained availability topic, backed by the client's last will: "online" while connected,
// "offline" the moment the broker notices the device has gone. Shared by every device.
#define HA_AVAILABILITY_TOPIC "heating_monitor/status"

#ifdef __cplusplus
extern "C" {
#endif

// Publishes the retained discovery config for the home, and marks every room and radiator as due
// to be announced. Safe to call on every reconnect -- each config topic is keyed on a device id,
// so a republish replaces rather than duplicates.
//
// The rooms and radiators are not announced here, because this is called from the MQTT event
// task and the room and radiator lists must not be walked from it: the HTTP task frees their
// nodes in remove_room() and remove_radiator(). ha_discovery_announce_pending() sends them from
// update_home() instead, which already walks both lists on its own task.
//
// Publishes at QoS 0: esp-mqtt can deadlock if a publish at QoS > 0 is issued from inside the
// MQTT event handler, which is where this is called from.
void ha_discovery_announce_all(esp_mqtt_client_handle_t client);

// Announces every room and radiator if ha_discovery_announce_all() has asked for it since the
// last time; otherwise does nothing. Call from a task that may walk the lists.
void ha_discovery_announce_pending(esp_mqtt_client_handle_t client, room_manager_t *room_manager,
                                   radiator_manager_t *radiator_manager);

// Announces every room and radiator now. Call from a task that may walk the lists.
void ha_discovery_announce_rooms_and_radiators(esp_mqtt_client_handle_t client,
                                               room_manager_t *room_manager,
                                               radiator_manager_t *radiator_manager);

// Announce a single room or radiator, after it is created or changed.
void ha_discovery_announce_room(esp_mqtt_client_handle_t client, const room_t *room);
void ha_discovery_announce_radiator(esp_mqtt_client_handle_t client, const radiator_t *radiator);

// Clear the retained config of a room or radiator, which removes its device from HA. Pass the
// MQTT name it was announced under -- on a rename, the old one.
void ha_discovery_remove_room(esp_mqtt_client_handle_t client, uint8_t room_id, const char *mqtt_name);
void ha_discovery_remove_radiator(esp_mqtt_client_handle_t client, uint8_t radiator_id, const char *mqtt_name);

// Publishes the retained "online" availability message. Call once the broker connection is up.
void ha_discovery_publish_online(esp_mqtt_client_handle_t client);

// The state topics the readings are published to and the discovery configs point at. Built in
// one place so the two cannot drift apart. The MQTT name is optional for a room, so an empty one
// falls back to the id.
void ha_room_state_topic(const room_t *room, char *out, size_t out_len);
void ha_radiator_state_topic(const radiator_t *radiator, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
