# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP-IDF firmware project for an ESP32-S3 OTBR (OpenThread Border Router) board that acts as a **Matter protocol controller** for monitoring a home heating system. It commissions and subscribes to Matter temperature sensors (over Thread), calculates heat loss, and serves a web UI for configuration and monitoring.

## Build Commands

### Prerequisites

Two environment variables must be set:
- `ESP_MATTER_PATH` — path to the [esp-matter](https://github.com/espressif/esp-matter) repository
- `IDF_PATH` — path to the ESP-IDF installation

### Full Build Sequence

**Step 1: Build the web app** (from the `html_app/` directory):
```sh
cd html_app
npm run build -- --emptyOutDir
```
This outputs `index.html`, `app.css`, and `app.js` into `html_data/`, which are embedded into the firmware binary.

**Step 2: Set the Thread Network Dataset** in `app_main.cpp` inside `nodes_post_handler`:
```c
char *dataset = "0e080000000000000000000300001935060004001fffc0..."
```

**Step 3: Build the firmware**:
```sh
idf.py build
```

### Flash and Monitor
```sh
idf.py flash monitor
```

### Connect to WiFi (from the ESP console after boot)
```sh
matter esp wifi connect {SSID} {Password}
```

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
2. **Web App** (`html_app/`) — React/TypeScript SPA, compiled and embedded as static files into the firmware via `EMBED_FILES` in `main/CMakeLists.txt`

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

**Other components:**
- `status_display.cpp` — LVGL-based SSD1681 e-ink display showing outdoor temperature
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

The built output (`html_data/index.html`, `app.css`, `app.js`) is embedded directly into the firmware binary — no separate file system is used.

### Key Configuration Files

- `sdkconfig` — ESP-IDF Kconfig settings (target chip, partition layout, Matter settings)
- `main/matter_project_config.h` — Matter-specific compile-time config
- `main/esp_ot_config.h` — OpenThread configuration
- `main/linker.lf` — Linker fragment (memory placement)
