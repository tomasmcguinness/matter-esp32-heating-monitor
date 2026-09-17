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

**CHIP external platform (`external_platform/ESP32_custom/`):**

The ESP32-S3 has no internal Ethernet MAC, but connectedhomeip's `ESPEthernetDriver::Init()` is written for one, so it does not compile for this target (espressif/esp-matter#1785). The repo therefore carries its own copy of the CHIP ESP32 platform layer with two local changes:

1. `BUILD.gn` — adds the `if (chip_enable_ethernet)` block that esp-matter's external-platform `BUILD.gn` is missing, so `ConnectivityManagerImpl_Ethernet.cpp` and `NetworkCommissioningDriver_Ethernet.cpp` are compiled.
2. `NetworkCommissioningDriver_Ethernet.cpp` — W5500 bring-up (SPI2: SCLK 13, MOSI 11, MISO 12, CS 14, INT 10, RST 9), plus `esp_netif_create_ip6_linklocal()` and `esp_route_hook_init()` on link-up. The route hook is essential: it is otherwise only installed by `ConnectivityManagerImpl_WiFi.cpp`, and without it lwIP ignores the border router's RIO options and the controller loses its route to the Thread sensors.

   The same file also carries the IPv6 multicast setup, which esp_netif does not do for Ethernet:
   - `NETIF_FLAG_MLD6` is set on the lwIP netif at link-up, before any address is configured. lwIP gates `nd6_adjust_mld_membership()` on this flag, so without it the solicited-node group for our own addresses is never joined.
   - `ff02::1` (all-nodes) is joined via `mld6_joingroup_netif()` on the first `IP_EVENT_GOT_IP6`. lwIP never joins it unprompted, so no MLD report is sent and an MLD-snooping switch prunes it on our port. That loses the border router's RAs, and because MLD general queries are themselves sent to `ff02::1`, it also means no group membership is ever refreshed — the switch eventually ages out mDNS (`ff02::fb`) too and commissionable-node discovery times out having heard nothing.

All `platform/ESP32/` includes in the copy are rewritten to `platform/ESP32_custom/` — without that, the copied sources pull in the original headers alongside the custom ones and fail with redefinition errors.

The root `CMakeLists.txt` copies the tree to `$ESP_MATTER_PATH/../platform/ESP32_custom` at configure time (this is what `CONFIG_CHIP_EXTERNAL_PLATFORM_DIR` resolves to), and registers the files under `CMAKE_CONFIGURE_DEPENDS` so edits trigger a re-copy.

Because this build sets `CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=n`, there is no Network Commissioning cluster to instantiate the driver, so `app_main()` calls `ESPEthernetDriver::GetInstance().Init(nullptr)` explicitly, after `esp_matter::start()`.

**Storage (`main/storage/`) — SD card history:**

Readings are sampled on a fixed cadence (5 s by default, configurable) and appended to a
MicroSD card, so the UI can show history rather than only "now".

History records **quantities, not sensors**. The sampler reads `home_manager`, so the series
follows the value — flow temperature, heat output, outdoor temperature — regardless of which
device supplied it. Replacing or re-pairing a heat meter therefore leaves the recorded series
continuous, where an archive keyed on `nodeId` would have orphaned it and started again. Rooms
and radiators follow the same rule, keyed on `room_id` and `radiator_id`.

There are three series, **all sampled on one shared interval**:

| Kind | File | Record |
|---|---|---|
| `KIND_HOME` | `/sdcard/home-YYYY-MM-DD` | `rec_home_t`, 28 B |
| `KIND_ROOM` | `/sdcard/room-<id>/YYYY-MM-DD` | `rec_room_t`, 8 B |
| `KIND_RADIATOR` | `/sdcard/rad-<id>/YYYY-MM-DD` | `rec_radiator_t`, 8 B |

The single interval is load-bearing, not just tidy: slot `i` is the same instant in every file
written that day, so a room's temperature, its radiators' flow and return, and the home's heat
and electrical power **join on index with no timestamp matching at all**.

Rooms and radiators store **measurements only** — no heat output, no mean water temperature, no
heat loss. Every one of those is a pure function of the stored temperatures plus configuration
already in NVS (`output_dt_50`, `predicted_heat_loss_per_degree`, target, room↔radiator
membership), so `calculations_manager`'s arithmetic is replayed at read time instead of frozen
on the card. That is what makes the archive durable: the heat-loss maths carried a sign bug
until 2026-09-14 that inflated every figure taken below freezing, and stored values would have
stayed wrong for the life of the card. The trade-off is that **configuration is not versioned**
— re-rate a radiator or move it between rooms and every historical derived figure changes
silently. Write a per-day config sidecar if that matters.

Do the deriving off the device (web app, ML pipeline), not in `history_api.c`: `pow()` per
radiator per slot over 17280 slots does not belong on the httpd task.

**Freshness is still a gap in the history, but no longer in the UI.** Sensors are subscribed
min 0 / max 60 (`subscription_manager.cpp`), so a 5 s sampler writes a mix of fresh and held
readings, and a steady column on a chart still looks identical to a sensor that died an hour
ago. `attribute_data_cb` now stamps `matter_node_t::last_seen` on every report, which
`GET /api/nodes` exposes as `lastSeen` and `Devices.tsx` shows as the subscription icon's
tooltip — so the device list distinguishes a live subscription from a silent one. That is
**per node, not per endpoint**: `endpoint_entry_t` still carries no timestamp, and the records
still reserve a field for `age_s`, which needs the stamp at endpoint granularity to fill.

`last_seen` is deliberately transient — it is absent from the NVS blob, so it reads 0 after a
restart and the API reports `null`, which the UI renders as "not seen since restart". It is
only stamped once `clock_is_valid()`, because before the first SNTP sync `time(NULL)` is in
1970.

- `sd_card.c` — mounts the card over **SPI3** (`CS 4, MISO 5, MOSI 6, CLK 7`). SPI2 is the
  W5500's and must not be shared. Failing to mount is not fatal: `sd_card_available()`
  stays false, the logger no-ops and the history endpoints report
  `{"storage":"unavailable"}`. Cards must be **FAT32** — ESP-IDF does not mount exFAT
  unless `CONFIG_FATFS_USE_EXFAT` is set, and cards over 32 GB usually ship exFAT.
- `time_sync.c` — SNTP plus `TZ=GMT0BST,M3.5.0/1,M10.5.0`. Nothing is logged until the
  first sync, because before it `time(NULL)` is in 1970 and every dated filename would be
  wrong. `CONFIG_ENABLE_SNTP_TIME_SYNC=y` is set as well, but only to unlock the
  `gettimeofday` branch of `ClockImpl::GetClock_RealTime` in the custom platform layer —
  it does not start an SNTP client.
- `history_format.h` — the on-disk layout. **Records carry no timestamp**: a 16-byte header
  holds `base_ts`, `interval_s` and `record_size`, so slot `i` is `base_ts + i*interval_s`
  and `offset = 16 + slot*record_size`. Lookup is O(1) arithmetic, with no index and no
  scan. `rec_home_t` is 28 bytes of 16-bit fields. `INT16_MIN`/`UINT16_MAX` mean "no
  reading" — zero cannot, because 0 W is a real reading.

  The record ends with **three reserved fields**, and they are the point of the design.
  `record_size` is what makes the offset arithmetic correct, so widening the record is a
  breaking change — the reader rejects a file whose `record_size` disagrees with its kind.
  A quantity added later fills a reserved field instead: `record_size` never moves, days
  already on the card stay readable and simply carry the sentinel in that column, and
  `history_api.c` names the columns in its response so the UI picks the new one up from the
  device. Field order is load-bearing in two places — `history_api.c` integrates fields 0
  and 1 for `energyWh`/COP, and `History.tsx` charts by position. **Append to the reserved
  tail; never reorder.** `history_unsigned_mask()` is a `uint16_t`, one bit per field, which
  caps any record at 16 fields.
- `history_logger.cpp` — an `esp_timer` samples every series into a RAM buffer; a separate
  task flushes once a minute (its own task because the esp_timer stack is 3584 bytes, too
  small for FATFS). Missed slots are padded with sentinels so the offset arithmetic stays
  true. The sampler races `attribute_data_cb`, which writes those fields from the CHIP event
  loop; the 64-bit readings are read unlocked and deliberately so — see the comment on
  `build_record()`.

  **Rooms and radiators are read from a snapshot, never from the manager lists.** Those are
  linked lists whose nodes the HTTP task frees (`remove_room`, `remove_radiator`), and the
  sampler runs on the esp_timer task — walking them there is a use-after-free, not a torn
  read, so the argument that makes `build_record()` safe does not carry over.
  `history_logger_snapshot()` copies both lists into fixed arrays of plain values, and is
  called from `update_home()` — the one function every calculation path ends in, which already
  traverses both lists on the calling task. Adding a manager field to the history means adding
  it to the snapshot, not making the sampler walk anything.
- `history_api.c` — `GET /api/history`, `/api/history/dates`, `/api/history/room?id=N`,
  `/api/history/room/dates?id=N`, and the same two for `/api/history/radiator`. Downsamples by
  striding slots and integrates energy over every slot, so kWh and COP do not change with the
  requested resolution. Energy and COP are emitted for `KIND_HOME` only — the other kinds
  carry no power column by design. Note `s_read_buf` is
  `READ_CHUNK_RECORDS × HISTORY_MAX_RECORD_SIZE`; scale the first down if the record ever
  widens.

  **The API is a chart endpoint and is lossy**: buckets average across `stride` slots and
  `MAX_POINTS` is 2000 against 17280 slots in a 5 s day. For ML, read the raw files off the
  card rather than training on bucket means.

Files are per local day. This needs `CONFIG_FATFS_LFN_HEAP=y` — the default 8.3 names cannot
hold them. Rooms and radiators get a directory each rather than a flat name, because the date
listing `opendir()`s and walks every entry: flat, a dozen radiators over a year would put
thousands of long-filename entries in one directory for every lookup to scan past. Home stays
flat where it already is, so history written before the other series existed is still found.

**`sdkconfig` is checked in and overrides `sdkconfig.defaults`**, so a Kconfig change has to be
made in both.

There is **no retention or pruning**: files accumulate indefinitely. At the 5 s default that is
484 KB/day for the home series plus 138 KB/day for each room and each radiator — roughly
3 MB/day (~1.1 GB/year) for a house with 8 rooms and 12 radiators.

The sampling interval is stored in the `home_manager` NVS blob and changed through
`PUT /api/home`. A change takes effect at the next local midnight, because a file's records
must stay spaced at the interval its header records. It applies to **all three series** — that
is what keeps them slot-aligned, so there is deliberately no per-kind interval.

**Other components:**
- `commands/` — Matter pairing and identify command wrappers
- `utilities/` — URL path token parsing (from the `path_variable_handlers` pattern)

**Data flow:**
1. Matter devices subscribe; attribute updates arrive at `attribute_data_cb`
2. `set_endpoint_measured_value` updates the node manager
3. `calculations_manager` recalculates heat loss for affected rooms and home totals
4. Results are published to **MQTT** and broadcast over the **WebSocket** to the web UI
5. `update_home()` hands the logger a flat snapshot of the room and radiator lists
6. In parallel, the history logger samples `home_manager` and that snapshot on its own cadence,
   writing one fixed-width record per slot to the SD card for the home, each room and each
   radiator — all on the same slot number

### Web App (`html_app/`)

React 19 + TypeScript, built with Vite. Uses:
- **React Router v7** for client-side routing (Home, Rooms, Radiators, Devices, History, Thread Network)
- **uPlot** for the History page's charts — chosen over heavier chart libraries because the
  whole bundle is embedded in the firmware image and ships in every OTA
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
