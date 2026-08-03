# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP-IDF firmware project for a **Waveshare ESP32-S3-ETH** board (16MB flash, octal PSRAM, W5500 SPI Ethernet) that acts as a **Matter protocol controller** for monitoring a home heating system. It commissions and subscribes to Matter temperature sensors (over Thread, via an external border router), calculates heat loss, and serves a web UI for configuration and monitoring.

Networking is **Ethernet only** — WiFi is compiled out (`CONFIG_ENABLE_WIFI_STATION=n`). BLE stays enabled because it is still used to commission the Thread sensors (`pairing_ble_thread`).

## Build Commands

### Prerequisites

Two environment variables must be set:
- `ESP_MATTER_PATH` — path to the [esp-matter](https://github.com/espressif/esp-matter) repository
- `IDF_PATH` — path to the ESP-IDF installation

### Full Build Sequence

**Step 1: Set the Thread Network Dataset** in `app_main.cpp` inside `nodes_post_handler`:
```c
char *dataset = "0e080000000000000000000300001935060004001fffc0..."
```

**Step 2: Build the firmware**:
```sh
idf.py build
```
A custom command in `main/CMakeLists.txt` runs `npm run build` in `html_app/` as part of this, so there is no separate web-app step. The output lands in `html_compiled_app/` and is embedded into the firmware binary via `target_add_binary_data`, so an OTA update carries the UI with it. npm only re-runs when something under `html_app/` has changed.

### Flash and Monitor
```sh
idf.py flash monitor
```

The board comes up on DHCP as soon as a cable is plugged in — there is no console provisioning step. The web UI is published over mDNS at `http://heating-monitor.local`.

### Web App Development
```sh
cd html_app
npm run dev      # Dev server (not connected to real hardware)
npm run lint     # ESLint
```

## Architecture

### Dual-Component Structure

The project has two distinct parts that must both be built:

1. **ESP-IDF Firmware** (`main/`) — C/C++ Matter controller firmware
2. **Web App** (`html_app/`) — React/TypeScript SPA, compiled to `html_compiled_app/` and embedded into the firmware binary via `target_add_binary_data` in `main/CMakeLists.txt`

### Firmware (`main/`)

**`app_main.cpp`** is the central file. It:
- Initialises the Matter controller stack (`esp_matter`, `esp_matter_controller_*`)
- Starts an HTTP server with REST API endpoints and a WebSocket endpoint
- Handles Matter attribute callbacks (`attribute_data_cb`) — the main data ingestion point
- Calls into managers to update state and trigger recalculation

**Managers** (`main/managers/`) are plain C structs with associated functions. All state is held in four global manager instances in `app_main.cpp`:

| Manager | Global | Responsibility |
|---|---|---|
| `node_manager` | `g_node_manager` | Matter nodes (linked list of `matter_node_t`), endpoints, measured values |
| `room_manager` | `g_room_manager` | Rooms, their assigned radiators and temperature sensor endpoint mapping |
| `radiator_manager` | `g_radiator_manager` | Radiators, their flow/return temperature sensor mapping and heat output |
| `home_manager` | `g_home_manager` | Home-level sensors (outdoor temp, heat source flow/return/flow-rate) |
| `calculations_manager` | (no global) | Derives heat loss and radiator output from the other managers |

All manager state is persisted to **NVS (Non-Volatile Storage)** via `save_*_to_nvs` / `load_*_from_nvs` functions.

**CHIP external platform (`ESP32_custom/`):**

The ESP32-S3 has no internal Ethernet MAC, but connectedhomeip's `ESPEthernetDriver::Init()` is written for one, so it does not compile for this target (espressif/esp-matter#1785). The repo therefore carries its own copy of the CHIP ESP32 platform layer with two local changes:

1. `BUILD.gn` — adds the `if (chip_enable_ethernet)` block that esp-matter's external-platform `BUILD.gn` is missing, so `ConnectivityManagerImpl_Ethernet.cpp` and `NetworkCommissioningDriver_Ethernet.cpp` are compiled.
2. `NetworkCommissioningDriver_Ethernet.cpp` — W5500 bring-up (SPI2: SCLK 13, MOSI 11, MISO 12, CS 14, INT 10, RST 9), plus `esp_netif_create_ip6_linklocal()` and `esp_route_hook_init()` on link-up. The route hook is essential: it is otherwise only installed by `ConnectivityManagerImpl_WiFi.cpp`, and without it lwIP ignores the border router's RIO options and the controller loses its route to the Thread sensors.

All `platform/ESP32/` includes in the copy are rewritten to `platform/ESP32_custom/` — without that, the copied sources pull in the original headers alongside the custom ones and fail with redefinition errors.

The root `CMakeLists.txt` copies the tree to `$ESP_MATTER_PATH/../platform/ESP32_custom` at configure time (this is what `CONFIG_CHIP_EXTERNAL_PLATFORM_DIR` resolves to), and registers the files under `CMAKE_CONFIGURE_DEPENDS` so edits trigger a re-copy.

Because this build sets `CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=n`, there is no Network Commissioning cluster to instantiate the driver, so `app_main()` calls `ESPEthernetDriver::GetInstance().Init(nullptr)` explicitly, after `esp_matter::start()`.

**Other components:**
- `commands/` — Matter pairing and identify command wrappers
- `utilities/` — URL path token parsing (from the `path_variable_handlers` pattern)

**Data flow:**
1. Matter devices subscribe; attribute updates arrive at `attribute_data_cb`
2. `set_endpoint_measured_value` updates the node manager
3. `calculations_manager` recalculates heat loss for affected rooms and home totals
4. Results are published to **MQTT** and broadcast over the **WebSocket** to the web UI

### Web App (`html_app/`)

React 19 + TypeScript, built with Vite. Uses:
- **React Router v7** for client-side routing (Home, Rooms, Radiators, Devices, Thread Network)
- **`react-use-websocket`** via `WSContext.jsx` for real-time data updates from the device
- **Bootstrap** (icons) for styling
- **vis-network** for Thread Network topology visualisation

Routes mirror the REST API structure: `/rooms`, `/rooms/:roomId`, `/radiators`, `/radiators/:radiatorId`, `/devices`, `/devices/:nodeId`, etc.

The built output (`html_compiled_app/index.html`, `app.css`, `app.js`) is embedded directly into the firmware binary — no separate file system is used — and served from the `_binary_*_start`/`_binary_*_end` symbols by `wildcard_get_handler` in `app_main.cpp`, which falls back to `index.html` for the SPA's client-side routes.

Because the files are listed explicitly in `main/CMakeLists.txt`, Vite must keep emitting exactly `index.html`, `app.css` and `app.js` (no content hashing — see `rollupOptions.output` in `html_app/vite.config.ts`). Adding a fourth asset means adding it to `WEB_APP_FILES` and giving it a handler.

### Key Configuration Files

- `sdkconfig` — ESP-IDF Kconfig settings (target chip, partition layout, Matter settings)
- `main/matter_project_config.h` — Matter-specific compile-time config
- `main/esp_ot_config.h` — OpenThread configuration
- `main/linker.lf` — Linker fragment (memory placement)
