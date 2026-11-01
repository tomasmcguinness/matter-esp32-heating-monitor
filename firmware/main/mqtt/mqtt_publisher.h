#pragma once

#include <stdbool.h>

#include "mqtt_client.h"

// Publishes to MQTT from a task of its own, so the CHIP task never waits on the broker.
//
// esp_mqtt_client_publish() takes the client's lock and writes to the socket, so it blocks for as
// long as the network does. Anything that blocks the CHIP task stops it draining its event queue,
// and CHIP aborts when that queue fills.
//
// Topics are held latest-value-only: a topic that is published again before the task reaches it
// has its payload replaced and keeps its place. MQTT state is the current value, so a burst of
// readings collapses into one publish, in the order the topics were first queued.

#ifdef __cplusplus
extern "C" {
#endif

// Creates the task and the topic table. Safe to call more than once.
void mqtt_publisher_start(void);

// Queues a publish and returns without touching the network. Copies topic and payload. QoS 0.
// Returns false if the table is full or there is no memory; the publish is dropped and counted.
bool mqtt_publisher_publish(esp_mqtt_client_handle_t client, const char *topic, const char *payload, bool retain);

// Runs fn(arg) on the publisher task, once however many times this is called before it gets
// there. For work that is only worth doing for the latest state, such as building a payload.
// There is one slot: every caller must pass the same fn and arg.
void mqtt_publisher_defer(void (*fn)(void *), void *arg);

#ifdef __cplusplus
}
#endif
