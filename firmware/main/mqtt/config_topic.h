#pragma once

#include "mqtt_client.h"
#include "managers/room_manager.h"
#include "managers/radiator_manager.h"

// The retained heating_monitor/config message: every room and every emitter with the settings a
// subscriber needs to make sense of the state topics -- ids, names, MQTT names and state topics,
// targets, heat loss, ratings, which radiators are in which room and the UFH shares.
//
// Home Assistant does not read it; discovery already tells HA what it needs. It is for anything
// else that stores the readings (the myheatpump site), which has to know that
// heating_monitor/rooms/living_room is room 10 to line it up with room-10/ on the SD card, and has
// to know the ratings and shares to work out radiator and UFH output itself.
//
//   {"schema":1,"device_id":"heating_monitor_<mac>",
//    "rooms":[{"id":10,"name":"Living Room","mqtt_name":"living_room",
//              "state_topic":"heating_monitor/rooms/living_room","target_c":21.0,
//              "heat_loss_w_per_c":24,"radiator_ids":[12],
//              "ufh_shares":[{"manifold_id":14,"pct":75}]}],
//    "emitters":[{"id":12,"name":"Living Room Radiator","mqtt_name":"living_room_radiator",
//                 "state_topic":"heating_monitor/radiators/living_room_radiator",
//                 "type":"radiator","rated_w":3464}]}
//
// mqtt_name is null when the room or emitter has none. type is "radiator" or "ufh". A manifold also
// has loop_flows_lpm, each loop's flow read off its gauges, and calibration_flow_lph, the heat meter's
// flow when they were read (null, with no loops, until calibrated). The device takes the manifold's
// flow as sum(loops) x heat meter flow / calibration_flow_lph, and its output as that flow times the
// drop across its probes. A manifold's rated_w is whatever was last entered and the device no longer
// uses it; it is kept so the schema does not change.
// Bump schema if a field changes meaning or goes away; adding a field does not need it.
//
// Like discovery, it is built on the task that runs update_home(), the one that may walk the room
// and radiator lists, and queued through mqtt_publisher, so nothing waits on the broker.

#define CONFIG_TOPIC "heating_monitor/config"

#ifdef __cplusplus
extern "C" {
#endif

// Asks for the config to be published on the next update_home(). Safe from any task, including
// the MQTT event handler. Call after anything the payload describes has changed, and on connect.
void config_topic_mark_pending(void);

// Publishes the config if config_topic_mark_pending() has been called since the last time;
// otherwise does nothing. Call only from a task that may walk the room and radiator lists.
void config_topic_publish_pending(esp_mqtt_client_handle_t client, room_manager_t *room_manager,
                                  radiator_manager_t *radiator_manager);

#ifdef __cplusplus
}
#endif
