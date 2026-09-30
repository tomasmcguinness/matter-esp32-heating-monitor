#pragma once

#include <sdkconfig.h>

#ifndef CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER

#ifdef CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
// Enable or disable whether this device advertises as a commissioner.
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONER_DISCOVERY 1
#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE

// Number of devices a controller can be simultaneously connected to
#define CHIP_CONFIG_CONTROLLER_MAX_ACTIVE_DEVICES 8

// Each subscribed node holds a CASE session for as long as its subscription lives. CHIP's default
// pool (MAX_FABRICS * 3 + 2 = 17) is smaller than the number of radiator sensors, so every new CASE
// session evicted a live one, whose reports then landed on an unknown session and its liveness timed
// out, and the resubscribe evicted another. Leave headroom for the web and companion clients.
#define CHIP_CONFIG_SECURE_SESSION_POOL_SIZE 32

// Every CASE handshake we start runs on an unauthenticated session until it completes, and every
// unsolicited unauthenticated message we receive -- ICD check-ins included -- needs one too. The
// default of 4 was held for minutes at a time by Sigma1s to sleepy sensors that never answered, and
// while it was full every other subscribe failed at once with CHIP_ERROR_NO_MEMORY and incoming
// check-ins were dropped ("UnauthenticatedSession exhausted").
#define CHIP_CONFIG_UNAUTHENTICATED_CONNECTION_POOL_SIZE 16

#endif // CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER
