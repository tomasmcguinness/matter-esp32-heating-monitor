#pragma once

// What this controller calls itself. DEVICE_NAME is reported by GET /api/companion/info.
//
// MDNS_HOSTNAME is **not currently advertised**: CONFIG_USE_MINIMAL_MDNS=y gives UDP 5353 to
// CHIP's own responder, so start_mdns_service() in app_main.cpp is commented out and nothing
// answers for heating-monitor.local. Reach the device by IP. These two are kept for the day that
// is re-enabled -- do not build a URL from them in the meantime, and do not tell a user to type
// one: whatever else on the network answers the name will be talked to instead of us.
#define DEVICE_NAME "Heating Monitor"
#define MDNS_HOSTNAME "heating-monitor"
#define MDNS_HTTP_PORT 80
