#pragma once

// What this controller calls itself. The hostname is published over mDNS once Ethernet has an
// address, so the web UI is reachable at http://heating-monitor.local without having to look up
// the DHCP lease, and all three are reported by GET /api/companion/info.
#define DEVICE_NAME "Heating Monitor"
#define MDNS_HOSTNAME "heating-monitor"
#define MDNS_HTTP_PORT 80
