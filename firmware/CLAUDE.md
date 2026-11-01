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

The board comes up on DHCP as soon as a cable is plugged in — there is no console provisioning step.

**Reach it by IP, not by name.** `CONFIG_USE_MINIMAL_MDNS=y` gives UDP 5353 to CHIP's own mDNS
responder, so `start_mdns_service()` in `app_main.cpp` is commented out and **nothing advertises
`heating-monitor.local`**. The name does not resolve to this board, and handing it to a client is
worse than useless: the client reaches whichever other machine on the network answers, gets that
machine's web page, and reports an unreadable response while this device logs no request at all.
That cost the companion app a working device list once already — see the `/api/companion/info`
comment in `companion_api.cpp`. Re-enabling `start_mdns_service()` means setting
`CONFIG_USE_MINIMAL_MDNS=n` first, in both `sdkconfig` and `sdkconfig.defaults`.

The IP is on the Settings page, and `GET /api/companion/info` reports it as `ip` and `url`.

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

**Managers** (`main/managers/`) are plain C structs with associated functions. All state is held in five global manager instances in `app_main.cpp`:

| Manager | Global | Responsibility |
|---|---|---|
| `node_manager` | `g_node_manager` | Matter nodes (linked list of `matter_node_t`), endpoints, measured values |
| `room_manager` | `g_room_manager` | Rooms, their assigned radiators and temperature sensor endpoint mapping |
| `radiator_manager` | `g_radiator_manager` | Radiators, their flow/return temperature sensor mapping and heat output |
| `home_manager` | `g_home_manager` | Home-level sensors (outdoor temp, heat source flow/return/flow-rate) |
| `heat_pump_manager` | `g_heat_pump_manager` | Latest readings from heat pump nodes: power, heating loops, hot water. Transient, not in NVS |
| `calculations_manager` | (no global) | Derives heat loss and radiator output from the other managers |

Other directories under `main/`: `storage/` (SD card history), `mqtt/` (Home Assistant), `mcp/`
(the read-only MCP server), `emoncms/` (the emoncms uploader), `commands/` and `utilities/`. `test_host/`, beside `main/`, holds
desktop tests for the parts of `mcp/` that need no board.

All manager state bar the heat pump's is persisted to **NVS (Non-Volatile Storage)** via `save_*_to_nvs` / `load_*_from_nvs` functions.

**Heat pump (`managers/heat_pump_manager.{h,cpp}`):**

Edit Home has a Heat Pump picker (device type 777), stored as `heat_pump_node_id` /
`heat_pump_endpoint_id` at the **end** of the `home_manager` NVS blob — `load_home_from_nvs()`
checks the blob's size first, because one saved by older firmware stops before them. Once one is
picked, `GET /api/home` and the `home` WebSocket channel carry a `heatPump` object and the Home
page renders `HeatPump.tsx`. With none picked the object is null and the section is absent, rather
than showing a "configure one" alert like the other sections.

It is **read only**: nothing is written to the device, and none of it reaches COP or the companion
API. COP still comes from the Electricity Meter picker — point that at the heat pump's endpoint,
which is an Electrical Sensor too, to use its power.

One thing does reach history: **whether the heat pump is heating or making hot water**.
`update_heat_pump_running()` in `app_main.cpp` sets `home_manager`'s `heating_running` and
`dhw_running` from the Heat bit of `ThermostatRunningState` — on a Water Heater endpoint that is
hot water, on any other Thermostat endpoint it is heating, ORed across endpoints of a kind.
`HeatDemand` is not used. Both are absent until such an endpoint reports and again when the node's
readings are dropped, so the record holds the sentinel rather than a stale mode. It runs after
every heat pump report, when the readings are forgotten, and when the pick changes. Both are
also in the MQTT home state (`heating_running`, `dhw_running`; true, false or null) and announced
to Home Assistant as binary sensors. `GET /api/home` and the `home` channel carry them as
`heatingRunning` and `dhwRunning` (same nulls), which `HomeStatus.tsx` shows as the mode icon in the
Home page's heading, beside the outdoor and average indoor temperatures.

The section is built from **everything on the heat pump's node**, not from the heat pump endpoint's
`PartsList`, which is never read:

| Endpoint device type | Shown as | Source |
|---|---|---|
| Heat Pump (777), the picked endpoint | Power | `ElectricalPowerMeasurement::ActivePower` |
| Temperature Sensor (770) | One card each | `endpoint_entry_t::measured_value`, as for any sensor |
| Thermostat (769) | A zone: temperature, target, mode, state | `Thermostat` cluster |
| Water Heater (1295) | A zone, plus operation mode, boost and heat demand | `Thermostat` + `WaterHeaterManagement` + `WaterHeaterMode` |

`send_subscription()` adds the Thermostat, Water Heater Management and Water Heater Mode paths for any node with the
Heat Pump device type. `attribute_data_cb` stores those reports — and the node's `ActivePower` — in
`g_heat_pump_manager` for **every** heat pump node, picked or not: a subscription only reports a
value again when it changes, so readings taken only after the pick would leave the section dashed
until each one next moved. A node's readings are dropped when its subscription ends
(`forget_heat_pump_if_unsubscribed()`), so the section dashes instead of showing a stale mode.

`WaterHeaterMode::CurrentMode` values are device-defined, so the zone's Operation card shows the
label the device's own `SupportedModes` gives the current one. `SupportedModes` is in the
subscription rather than a one-off read: it never changes, so it costs one entry in the priming
report, and it arrives alongside the `CurrentMode` it labels. Up to `HEAT_PUMP_MAX_MODES` (8) are
kept, value and label only — the mode tags are dropped. A mode missing from the list shows as
"Mode N".

The sensors are told apart by **semantic tags**, since a composed device's sub-parts carry no
FixedLabel. The node interview reads `Descriptor::TagList` and the first tag with a label names the
endpoint, unless a FixedLabel or bridged NodeLabel already has. That happens at commissioning only,
so a node paired before this was added shows "Endpoint N" until it is re-paired.

**CHIP external platform (`external_platform/ESP32_custom/`):**

The ESP32-S3 has no internal Ethernet MAC, but connectedhomeip's `ESPEthernetDriver::Init()` is written for one, so it does not compile for this target (espressif/esp-matter#1785). The repo therefore carries its own copy of the CHIP ESP32 platform layer with two local changes:

1. `BUILD.gn` — adds the `if (chip_enable_ethernet)` block that esp-matter's external-platform `BUILD.gn` is missing, so `ConnectivityManagerImpl_Ethernet.cpp` and `NetworkCommissioningDriver_Ethernet.cpp` are compiled.
2. `NetworkCommissioningDriver_Ethernet.cpp` — W5500 bring-up (SPI2: SCLK 13, MOSI 11, MISO 12, CS 14, INT 10, RST 9), plus `esp_netif_create_ip6_linklocal()` on link-up. Something has to honour the Route Information Options in the border routers' RAs, or the controller has no route to the Thread sensors. That is lwIP itself: `CONFIG_LWIP_IPV6_ND6_ROUTE_INFO_OPTION_SUPPORT=y`. It replaced CHIP's route hook (`esp_route_hook_init()`, `route_hook/`), which picked one gateway for the whole Thread prefix and never invalidated lwIP's cached next hops. The two are exclusive — CHIP's `ENABLE_ROUTE_HOOK` depends on the lwIP option being off — so the driver's `esp_route_hook_init()` call is behind `#ifdef CONFIG_ENABLE_ROUTE_HOOK` and the hook sources are not built. Turning the lwIP option off brings the hook back.

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

**A reading does not outlive its subscription.** When a node's subscription ends or fails to
establish, `forget_readings_if_unsubscribed()` in `app_main.cpp` clears `has_measured_value` on
every endpoint of the node and drops whatever was fed from it — a room's temperature, a
radiator's flow and return (and the output derived from them), the home's heat meter and
electrical meter readings. History then records the sentinel, MQTT publishes null and the UI
dashes; the priming report of the next subscription sets everything again. Readings are nullable
throughout: `endpoint_entry_t::has_measured_value`, `room_t::has_current_temperature`,
`radiator_t::has_flow_temperature` / `has_return_temperature` / `has_mean_water_temperature` /
`has_heat_output`, and `get_endpoint_measured_value()` returns whether there was one. 0 is a
reading, not a placeholder. `calculations_manager` works nothing out from a missing input: a
room's heat loss figures are zeroed and flagged absent (`has_predicted_heat_loss`,
`has_measured_heat_loss`) without an outdoor temperature, and the measured ones without a room
temperature and a radiator output above zero as well.

**A radiator's output is zero unless the heat pump is space heating.** The figure is derived from
temperatures, and a radiator stays warm after the pump stops or turns to hot water, so
`update_radiator_outputs()` reports 0 W unless `has_heating_running && heating_running`. A heating
state that is not known — no heat pump picked, or none reported — counts as not heating, so a
system with no heat pump shows 0 W throughout. This is a zero, not a missing value: a radiator
short of a flow or return reading still has no output at all. `update_heat_pump_running()`
recalculates every radiator and pushes it on the `radiator` WebSocket channel when heating starts
or stops, since no temperature report need arrive at that moment. A room whose radiators total
0 W has no measured heat loss, rather than one of 0 W/°C.

**Freshness within a live subscription is still a gap in the history.** Sensors are subscribed
min 0 / max 60 (`subscription_manager.cpp`), so a 5 s sampler writes a mix of fresh and held
readings, and a steady column on a chart still looks identical to a sensor that has gone quiet
without its subscription having been torn down yet. `attribute_data_cb` now stamps `matter_node_t::last_seen` on every report, which
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
- `time_sync.c` — SNTP plus `TZ`, both from the **time settings** (`time_settings.{h,c}`, NVS
  namespace `time`, keys `zone` and `ntp`; `GET`/`PUT /api/time/config`, `TimeSettings.tsx`).
  The default is `Europe/London` (`GMT0BST,M3.5.0/1,M10.5.0`) and `pool.ntp.org`. There is no tz
  database, so a zone is an IANA name from the table in `timezones.c`, mapped to a POSIX string.
  Both are read **once per boot**, on the first call to `time_sync_start()` or
  `time_sync_zone_name()`, so a change takes effect on restart: the logger, subscription log and
  totals all work out local midnight on other tasks, and must not see the zone change mid-day.
  A zone change still means a day file can be reopened whose header `base_ts` is the old zone's
  midnight, so `write_series_day()` places each sample by its timestamp when the two differ,
  rather than at a slot counted from the new midnight. Nothing is logged until the
  first sync, because before it `time(NULL)` is in 1970 and every dated filename would be
  wrong. `CONFIG_ENABLE_SNTP_TIME_SYNC=y` is set as well, but only to unlock the
  `gettimeofday` branch of `ClockImpl::GetClock_RealTime` in the custom platform layer —
  it does not start an SNTP client.
- `history_format.h` — the on-disk layout. **Records carry no timestamp**: a 16-byte header
  holds `base_ts`, `interval_s` and `record_size`, so slot `i` is `base_ts + i*interval_s`
  and `offset = 16 + slot*record_size`. Lookup is O(1) arithmetic, with no index and no
  scan. `rec_home_t` is 28 bytes of 16-bit fields. `INT16_MIN`/`UINT16_MAX` mean "no
  reading" — zero cannot, because 0 W is a real reading and 0.00 °C a real temperature. That
  holds for every kind: rooms and radiators carry `has_*` flags, the logger writes the sentinel
  when one is false, and `history_field_is_null()` treats only the sentinel as null. Room and
  radiator files written before 2026-10-01 recorded "no reading yet" as 0, and those slots chart
  as 0.00 °C.

  The record ends with **reserved fields**, and they are the point of the design. There were
  three; `heating_running` took the first, which is why it sits after the electrical columns
  rather than next to `dhw_running`.
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

  The home response's tail also carries `modes`: the same energy over only the slots recorded as
  heating and as hot water, each with its own `cop`. `GET /api/history/totals` gives the same
  three figures -- `overall`, `heating`, `hotWater` -- summed over every day on the card, with
  `from`, `to` and `days`. It is routed inside the `/api/history/*` wildcard, so it costs no URI
  handler.

  **The API is a chart endpoint and is lossy**: buckets average across `stride` slots and
  `MAX_POINTS` is 2000 against 17280 slots in a 5 s day. The exception is **`points=0`, which
  returns every slot unaveraged** (stride 1). That is what the off-device Node server in
  `mcp_server/` reads; the MCP server in the firmware reads the day files directly and does not
  use it. It is about 2 MB for a 5 s day and blocks the single httpd task while it streams, so it
  is for a caller that fetches a completed day once and keeps it -- never for a chart. For ML, the raw files off the card are still the better source.

- `history_totals.{h,c}` — heat and electrical energy split by heat pump mode, behind the three
  COPs on the History page: heating, hot water and overall (SCOP). Every COP is **energy out over
  energy in for the slots covered**, never a mean of the `cop_x100` column. A slot is hot water if
  `dhw_running` is 1, else heating if `heating_running` is 1, else neither; `overall` takes every
  slot, so standby lowers the overall figure only. A day recorded before `heating_running` existed
  contributes to `overall` alone.

  `history_totals_add()` is the one place that arithmetic lives: `stream_file()` calls it per slot
  for the day view, and `history_totals_scan_file()` runs it over a whole day.

  A season cannot be re-read per request, so completed days are summarised once into
  **`/sdcard/cop-totals`** — an 8-byte header and one 64-byte record per day, appended oldest
  first. This is a **cache, not part of the archive**: everything in it is derived from the day
  files, so deleting it loses nothing and it refills (from the next restart or midnight, if it had
  already caught up), and bumping `HISTORY_TOTALS_VERSION` when
  the arithmetic changes discards it the same way. That is how it sits alongside the
  measurements-only rule above. The logger's flush task calls `history_totals_backfill_one()`
  after each flush, which summarises **one day a minute**, so a year of backlog takes about six
  hours and never stalls sampling; once caught up it walks the directory once a day. Today is
  never cached — `totals_handler` scans today's file on each request. The file is deliberately
  not named `home-*`, because `dates_handler` picks out home days by that prefix. The flush
  task's stack went from 4096 to 6144 for this.

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

**The subscription log (`storage/subscription_log.{h,cpp}`) — the Diagnostics page:**

History records quantities, so a lost subscription shows there only as a gap with no owner and no
cause. This records the subscriptions themselves, per node, in `/sdcard/subs/YYYY-MM-DD`: an 8-byte
header (`SUBL`, version, record size) and fixed 24-byte `sublog_record_t`s (node id, ts, CHIP error
code, event), appended in the order they happen.

- **What is recorded.** `node_subscription_established_cb` → `ESTABLISHED`;
  `node_subscription_terminated_cb` → `TERMINATED` when it ends the node's current established
  subscription, `ATTEMPT_FAILED` when the id is 0 (an attempt that never established), nothing
  when it is for a subscription the node has already replaced; `node_subscribe_failed_cb` →
  `ATTEMPT_FAILED` with `error.AsInteger()`; `unpair_node()` → `REMOVED`. One failed attempt can
  be reported by both a termination and a failure callback, so failure counts can run high.
- **No I/O on the CHIP event loop.** `subscription_log_record()` only pushes onto a 256-entry RAM
  ring, stamped with the uptime and, once `clock_is_valid()`, the wall time. The history flush task
  calls `subscription_log_flush()` once a minute, which converts any uptime-only events to wall
  time (so events from before SNTP land at the right time) and writes them. With no card the events
  are dropped. Before the clock is set they are held.
- **Every day file states its starting point.** A new file opens with `DAY_START` and one
  `UP_AT_START` per node subscribed at that moment, kept by the flush task from the events it has
  written. `subscription_log_init()` writes `BOOT` first, before `subscription_manager_init()`,
  and a boot means every node is unsubscribed.
- **Monitor downtime is visible.** Every flush also writes `ALIVE`, so the last `ALIVE` before a
  `BOOT`, or the last in a past day's file, is when the monitor itself stopped. That is about
  35 KB a day.

`GET /api/history/subscriptions?date=` is routed inside the `/api/history/*` wildcard, so it costs
no URI handler. It streams the day's events, plus any still in the ring, as
`{storage, date, now, events: [{ts, event, nodeId?, lastAlive?, code?, error?}], lastAlive}`.
`ALIVE` records are not sent: each `boot` carries the last one before it as `lastAlive`. Error codes
are decoded with `chip::ErrorStr(err, false, storage)`, the overload that takes caller storage,
because the plain one shares a static buffer with the CHIP task.

`Diagnostics.tsx` (`/diagnostics`) replays the events into a 24-hour bar per node: subscribed,
not subscribed, or monitor off. Each node also shows the % of the day subscribed, the number of
drops and failed attempts, and its longest outage, plus a list of outages with their failure
reasons. Nodes are sorted worst first. Names come from `/api/nodes`, and a node that is no longer
paired shows as "Removed device". Today refreshes every minute.

**The companion API (`companion_api.{h,cpp}`, `node_api.h`):**

`/api/companion/*` is **not our API**. It is the contract of the generic
[Matter Controller Companion](https://github.com/tomasmcguinness/matter-controller-companion-app)
(MCC) iOS app, which works against any self-hosted controller, and its paths, bodies and status
codes come from that repo's `README.md`, `iOS/Shared/MCCClient.swift` and `iOS/Shared/Models.swift`.
Change the app first; do not reshape these to suit the firmware.

| Method | Path | Notes |
|---|---|---|
| `GET` | `/api/companion/info` | Reachability. The app ignores the body and takes any 2xx |
| `GET` | `/api/companion/nodes` | The node array |
| `POST` | `/api/companion/nodes` | `{inUse, setupCode}` → `{nodeId}`, held open until pairing finishes |
| `PUT` | `/api/companion/nodes/{nodeId}/update` | `{name}` |
| `DELETE` | `/api/companion/nodes/{nodeId}` | Unpair |

There is **no authentication**. There was, briefly — a `pairing_manager` issuing a token that
`GET /api/info` handed out as a QR code for the old bespoke app in `companion_app/`. All of it is
gone: MCC is added by address and the generic API has no place to put a credential. `POST` and
`DELETE` here will commission and unpair for anyone who can reach port 80, exactly as the web UI's
equivalents already did.

**The operations are shared; the payloads are not.** `node_api.h` carries
`commission_node()`, `unpair_node()` and `rename_node()`, implemented in `app_main.cpp` so the
commissioning statics and the CHIP pairing callbacks stay in one translation unit, plus
`read_json_body()` — the NUL-terminating body read most of the older handlers do by hand and get
subtly wrong. Both APIs drive the same Matter code through those.

The node array is built **twice, on purpose**: `build_nodes_json()` in `app_main.cpp` for
`/api/nodes`, and `build_companion_nodes_json()` in `companion_api.cpp` for
`/api/companion/nodes`. That is what the separate namespace is for. `/api/nodes` feeds our web UI
and carries whatever the UI needs — `lastSeen` today, more later — and none of that may leak onto
the contract; equally, the contract must not dictate what the UI can have. Sharing one builder
between them collapses that distinction, and did: the UI's `lastSeen` ended up on the contract
endpoint. Add a field to one, not both.

`build_companion_nodes_json()` sends **less than the contract allows, on purpose**. Every field
bar `nodeId` is optional with a documented default, and the app only uses the name (`nodeName`,
falling back to `productName` then the id), `vendorName` for the subtitle, `hasSubscription`, and
the endpoints with their device types — the same set `matter-esp32-controller` and
`matter-esp32-home-energy-manager` send. So `isIcd`, `powerSource`, `batteryPercent`,
`batteryVoltage` and `measuredValue` are left off: they are real readings, but the app doesn't
show them, and a list the app refreshes on every pull shouldn't carry a per-endpoint sensor feed.
They stay on `/api/nodes`, where the web UI does show them. `extAddress` is left off for a
different reason — there is no working source for it, since the
`ThreadNetworkDiagnostics::ExtAddress` subscription reads 0 for every node, and a missing value
and a 0 decode identically on the app's side. Restore it when that read is fixed.

The rule for both directions: a field goes on the companion array because the app has a use for
it, and on `/api/nodes` because the web UI does. Neither is a reason to add it to the other.

`companion_api.cpp` includes `device_identity.h`, not `app_main.h`: the latter declares
`attribute_data_read_done` in terms of `ScopedMemoryBufferWithSize` and only compiles after
`app_main.cpp`'s `using namespace` block, so it cannot be included from anywhere else.

`companion_api_register()` must run **before** the `/*` wildcard, like `history_api_register()` and
`status_api_register()`. `config.max_uri_handlers` is a hard cap, now 44 with 40 registered — the
breakdown is in the comment beside it in `start_webserver()`; count the registrations before adding
more.

**The MCP server (`mcp/`):**

`POST /mcp` is a read-only [MCP](https://modelcontextprotocol.io) server over the recorded
history, so an AI assistant can analyse cycling, COP, the effect of heating-curve changes, and how
individual rooms and radiators behave. Add it to
Claude Code with `claude mcp add --transport http heating-monitor http://<board-ip>/mcp`.

| Tool | Returns |
|---|---|
| `list_series` | Every series: id, name, unit, native interval, earliest and latest timestamps |
| `get_series` | Series over a range as `{ interval_seconds, timestamps, series }`; at most 1000 points each unless an interval is named, never more than 5000 |
| `summarise_cycles` | Each compressor run in a range, plus a period summary |
| `compare_periods` | Two period summaries side by side, each with its mean outdoor temperature |
| `list_rooms` | Each room with its radiators: id, name, target, ratings, series ids, first and last recorded day |

The cycle tools and `list_series` only read `/sdcard/home-*`. `get_series` also reads a room's or
a radiator's day files, and `list_rooms` reads the configuration. Nothing in `mcp/` writes to the
card, changes a manager or can reach the heat pump, and that is the property to keep: a tool that
changes anything does not belong here.

**Rooms and radiators.** `get_series` takes `room_<id>_temp_c`, `radiator_<id>_flow_temp_c` and
`radiator_<id>_return_temp_c` alongside the home ids, resolved by `mcp_series_resolve()`. Each
distinct room or radiator is one more pass over the card, bucketed **by timestamp**, so a file that
started mid-day still lines up with the home's. An id with no files returns nulls, not an error — a
deleted room keeps its history and looks no different from one that never existed. Because every
one is a file per day, a call may read at most `MCP_MAX_INSTANCE_DAYS` (124) room-or-radiator days,
roughly what 31 days of the home series already costs.

**Radiator heat output is not offered as a series**, for the reason it is not on the card: it is
derived, and the `pow()` belongs off the device. `list_rooms` gives the rated output and its
description gives the formula, including that the output is zero unless `heating_running` is 1.

`mcp_directory.h` is the server's one view of the managers: `mcp_directory_rooms()` and
`mcp_directory_radiators()` copy names, targets, ratings and room membership out by value.
`mcp_directory.cpp` implements them against `g_room_manager` and `g_radiator_manager`; the split
exists because `mcp_server.c` is C and is also built on a desktop, where `test_server.c` supplies
them. They are only safe on the HTTP task, which is the task that adds and frees those lists'
nodes. Names are user text, so they go through `out_json_string()`. What they report is **today's
configuration** — history from before a radiator was re-rated or moved is not marked. There is **no authentication**, like the rest of the HTTP API.

- `mcp_server.c` — the transport and the tools. Streamable HTTP, **stateless**: one JSON response
  per POST, no session id, no SSE. `GET /mcp` is registered only to answer 405; without it the
  request falls through to the web app's wildcard and an MCP client is handed `index.html`. A
  tool's result is JSON inside a JSON string, so `mcp_out_t` has an `escape` mode and the tool
  code writes its JSON plainly. **Everything is validated and computed before the first byte is
  sent**, because once the chunked response has started an error can no longer be reported.
  Buckets and runs are held in PSRAM for the length of a call (`big_alloc`).
- `mcp_tools.json` — the tool definitions, embedded with `target_add_binary_data(... TEXT)` so the
  schemas are JSON rather than an escaped C string. Descriptions carry the units and tell the
  model to prefer `summarise_cycles` for cycling questions.
- `mcp_cycles.{h,c}` — run detection. A run is electrical input at or above a threshold (300 W);
  runs under 120 s apart are merged, runs under 60 s dropped, and a run is split where the mode
  changes. It is fed one slot at a time and keeps **sums, not samples**, so a month costs the same
  memory as a night. Heat samples above 3x the rated output, or implying an instantaneous COP
  above 10, are left out of heat averages, energy and COP and counted per run. A run's COP divides
  energies over only the samples whose heat was kept.
- `mcp_series.{h,c}` — the series table (ids, units, `rec_home_t` field positions) and the point
  caps. `mcp_time.{h,c}` — ISO 8601 in and out.

Things that are deliberately so:

- **Timezones are the configured zone and UTC only.** The board has one zone in `TZ` and no tz
  database. `mcp_server_register()` is given its IANA name (and the firmware version, for
  `serverInfo`), and `mcp_tools.json` writes `{{LOCAL_ZONE}}` wherever the schemas name it, which
  `tools/list` fills in. A caller elsewhere passes timestamps with an explicit offset.
  `scan_series()` opens one day file beyond each end of the range and never goes back in time,
  because files written before a zone change have the old zone's day boundaries.
- **A call covers at most 31 days** (`MCP_MAX_RANGE_DAYS`). Each day is a 484 KB read on the httpd
  task, and everything else the web server does waits behind it.
- **The rated output is a tool argument**, `rated_output_w`, defaulting to a placeholder 8000 W.
- **No target flow temperature and no defrost state** -- neither is recorded. Mode is the
  `heating_running` / `dhw_running` flags; a run on a day from before they were logged is `unknown`.
- A mode change shorter than `min_run_seconds` is folded into its neighbour, and a sample in
  neither mode takes the mode around it: the flags are thermostat demand and can drop a moment
  before the compressor does. A changeover from hot water into heating is not a compressor start.

`mcp_cycles`, `mcp_series` and `mcp_time` have no ESP-IDF in them, so **`test_host/run.sh` tests
them on a desktop** against a synthetic night. It also builds the real `mcp_server.c` against
stand-in headers (`test_host/stubs/`) and a day file in a temporary directory, and parses every
response back as JSON. Run it after any change under `mcp/`. `test_server --serve` answers
requests from stdin, for putting that build behind an HTTP shim and a real MCP client.

`mcp_server_register()` must run **before** the `/*` wildcard, and takes two of the
`max_uri_handlers`.

**The emoncms uploader (`emoncms/emoncms_uploader.{h,cpp}`):**

Posts nine home-level readings to an emoncms server — emoncms.org by default — as one node:
`POST <base_url>/input/post` with `Authorization: Bearer <write key>` and a form body of `node`,
`fulljson` and `time`. A 200 whose body is the bare text `ok` (what emoncms.org sends) or
`{"success": true}` counts as accepted; any other body is a rejection, because emoncms answers
some refused posts with a 200 too.

**The set of inputs is deliberately small and fixed**, because each one becomes a feed that
emoncms.org charges for: no rooms, no radiators, no COP. It lives in one place, the `INPUTS` table
— key, reader and decimals per input. The readers apply the same conversions as the MQTT home
payload (0.01 °C, mW), except flow, which the heat meter reports in l/h and emoncms is sent in
l/min. A reading whose `has_*` flag is false is **left out of the payload**, never sent as a
placeholder, and a sample with no readings at all is not posted.

Everything runs on the uploader's own task, woken by an `esp_timer` at the configured interval and
by task notifications for a configuration change or a test. It reads `home_manager` unlocked, one
read per field, exactly as `history_logger`'s `build_record()` does and for the same reason.
Nothing is sampled until `clock_is_valid()`, and every post carries the `time` it was sampled at.

Each sample is pushed onto a ring buffer in PSRAM and the buffer is sent **oldest first, one post
per pass of the task loop**, so a fresh reading and a backlog take the same path. A failed post
stays at the head and is retried after 5 s, doubling to 60 s; while that goes on new samples queue
behind it, and when the buffer is full the oldest is dropped (counted as `dropped`). The buffer
covers an hour — `3600 / interval_s` entries — so an outage shorter than that is backfilled with
the original timestamps. It is RAM only: a restart loses it. The HTTP connection is kept open
between posts and closed on any failure or configuration change.

Configuration is in the NVS namespace `emoncms` (`enabled`, `base_url`, `api_key`, `node`,
`interval_s`), separate from the manager blobs, and a change applies without a restart.

| Method | Path | Notes |
|---|---|---|
| `GET` | `/api/emoncms/config` | `key_set` in place of the key |
| `PUT` | `/api/emoncms/config` | Absent fields are kept; an empty `api_key` keeps the stored one |
| `GET` | `/api/emoncms/status` | Counters, last error, buffer fill, `inputs` and `last_payload` |
| `POST` | `/api/emoncms/test` | Sends the current readings once and returns emoncms's answer |

**The write key never leaves the device.** `emoncms_uploader_get_config()` returns it blanked, it is
never logged, and a failed post logs its HTTP status only — the response body goes to the status
endpoint, not the log. `POST /api/emoncms/test` works while uploading is disabled, so a key can be
checked before it is switched on; it parks the httpd task until emoncms answers or the 10 s timeout
passes, like commissioning does.

On the Settings page this is `EmoncmsSettings.tsx`. The last payload is listed against the
`inputs` the status response names, so a reading that was not sent shows as "missing" rather than
just being absent from the table.

**MQTT / Home Assistant (`mqtt/ha_discovery.{h,cpp}`):**

The home, every room and every radiator are announced to Home Assistant with retained
device-based discovery on `homeassistant/device/<id>/config`. The home's id is
`heating_monitor_<eth-mac>`. Rooms and radiators append `_room_<mqtt_name>` /
`_radiator_<mqtt_name>` (the id when there is no MQTT name) and link back with `via_device`. The
MQTT name also sets each entity's `default_entity_id` (`sensor.master_bedroom_current_temperature`,
`sensor.master_bedroom_radiator_flow_temperature`), slugified to what HA accepts. Since the key
can change, the PUT handlers copy the old MQTT name before updating, and on a rename remove the
device under the old name before announcing the new one. HA drops that device's history with
it. State goes to `heating_monitor/home`,
`heating_monitor/rooms/<mqtt_name>` and `heating_monitor/radiators/<mqtt_name>`, falling back to
`room_<id>` / `radiator_<id>` when the MQTT name is empty. Build these topics with
`ha_room_state_topic()` / `ha_radiator_state_topic()` so discovery and publishing agree. A reading
that isn't there is published as JSON `null`, never 0.

**The broker is configured from the Settings page**, not in the source. `mqtt/mqtt_settings.{h,cpp}`
keeps `url`, `username` and `password` in the NVS namespace `mqtt`; `GET /api/mqtt/config` returns
`password_set` in place of the password, and `PUT` treats an empty password as "keep the stored
one", the same way the emoncms key is handled. The URL must be `mqtt://` with no credentials in it,
or empty, which turns MQTT off: `start_mqtt_service()` then creates no client, `_mqtt_client` stays
NULL and `calculations_manager` skips its publishes.

A `PUT` applies straight away through `restart_mqtt_service()`, which stops the client,
reconfigures it with `esp_mqtt_set_config()` and starts it again. The client is **never
destroyed**, because its handle is held and published on by other tasks. One consequence:
`esp_mqtt_set_config()` ignores a NULL field, so *removing* a username or password only reaches the
broker after a restart. Changing one applies immediately.

On connect, the MQTT event handler announces only the home. Rooms and radiators are flagged and
sent by `ha_discovery_announce_pending()` from `update_home()`, for the same reason the history
logger snapshots: the room and radiator lists must not be walked from another task. The HTTP
handlers announce or remove single rooms and radiators directly when they change. Adding an
entity is one row in the `*_SENSORS` tables, plus the matching key in `calculations_manager.cpp`.
A row with `binary` set is a true/false in the payload and is announced as a `binary_sensor`,
whose template maps it to `ON` / `OFF` and a null to `None` (unknown).

**Keeping the CHIP task's event queue from filling:**

lwIP's task posts every incoming UDP packet to the CHIP platform event queue, and **CHIP aborts if the
post fails** (`Failed to post event to CHIP Platform event queue`, then `abort()` in
`ScheduleLambdaBridge`). Anything that holds the CHIP task up while packets arrive restarts the
board, and CASE crypto during resubscription does that on its own, so three things guard it.
Don't do slow work on the CHIP task, which includes `attribute_data_cb` and everything it calls.

- **`BoundedQueueFilter` (`utilities/bounded_queue_filter.h`)** counts the packets queued and drops new
  ones past 96, or 24 for mDNS, so UDP alone can't fill the 128-entry queue
  (`CONFIG_MAX_EVENT_QUEUE_SIZE`). Senders retransmit, so a drop is a delay, and a backlog that drained
  is logged as `Dropped N UDP packets`. `install_mdns_queue_filter()` installs it, and a
  `static_assert` there keeps the limits below the queue size.
- **`mqtt/mqtt_publisher.{h,cpp}`** is the way to publish. `esp_mqtt_client_publish()` blocks on the
  client's lock and the socket, so calling it on the CHIP task lets a slow broker stall it. The
  publisher's task holds one latest-value slot per topic, 96 in all, so a burst becomes one publish.
  It also builds the home payload, through `mqtt_publisher_defer()`, reading `home_manager` unlocked
  as the history sampler does. The exception is `ha_discovery_publish_online()`, which runs on the
  MQTT task.
- **`schedule_home_broadcast()`** debounces the home WebSocket push by 250 ms, for use in
  `attribute_data_cb`, which runs once per attribute. CHIP task only: HTTP handlers call
  `broadcast_home_state()` directly.

Crash backtraces need no coredump: the panic is on the console, and `xtensa-esp32s3-elf-addr2line -pfiaC
-e build/heating-monitor.elf <addresses>` decodes it, provided `ELF file SHA256` in the log matches
`sha256sum` of that file.

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
- **React Router v7** for client-side routing (Home, Layout, Rooms, Radiators, Devices, History)
- **uPlot** for the charts — chosen over heavier chart libraries because the whole bundle is
  embedded in the firmware image and ships in every OTA

`HistoryChart.tsx`'s `Chart` wraps every plot in a `ChartCard`, whose header carries the title, so
uPlot is given no `title` of its own. A chart's empty and error states have to render their own
`ChartCard` — otherwise the panel disappears when there is nothing to draw and the page shifts
under it. `Chart` has one y axis by default; a series with `axis: "right"` moves to a second one,
which is how a radiator's output in watts sits alongside its flow and return in °C.

`Chart` also takes `modes`: the spans of the day the heat pump spent heating and making hot water,
drawn by a uPlot `draw` hook as an 8px strip just under the plot, between the x axis and its time
labels (the axis's ticks and height are enlarged to make the room), with a key under it. It is
outside the plot area so that it cannot cover a line running along the bottom.
Idle time is blank. Time with neither column recorded -- the heat pump's subscription was lost, or
the device was down -- is a grey "No data" span, so an outage is not read as the heat pump being
off; the key lists it only when there is some. A day with no mode recorded at all has no strip.
`modeSpans()` in `historyData.ts` builds them from the home
series' `heatingRunning` and `dhwRunning` columns, found by name; hot water wins when both are on.
`History.tsx` derives them from the response it already has, and the room and radiator charts get
them from `useHeatPumpModes()`, which fetches the home series for the day. The spans are absolute
times, so nothing is joined against the chart's own points. A bucket is an integer mean, so it
reads as on only when the mode held throughout — a run is drawn up to one bucket short at each end.

`History.tsx`'s first row of cards is the day's heat by mode -- Heating and Hot water, from
`modes.*.heatWh` -- and Electricity in, which is the whole day's. The day's total heat is not
shown, and heat metered in neither mode is in neither card. A mode card dashes when its COP card
does.

`History.tsx` shows three COP cards for the day being viewed (Heating, Hot water, Overall, from the
response's `modes` and `cop`) and three across every recorded day (Heating, Hot water, SCOP, from
`GET /api/history/totals`, fetched once and independent of the date picker). A dash is "no
electricity to divide by", which is also what a day from before the mode was recorded shows for
heating and hot water. Unlike the mode bar, these are integrated on the device over every slot,
so the bucket trimming above does not affect them. Mixing units on
the *same* axis is still not possible, so `History.tsx` keeps one chart per unit.

**Recorded quantities are charted; derived ones are computed in the browser.** A radiator's heat
output is not on the card — `rec_radiator_t` stores flow and return only — so `RoomHistory.tsx`
derives it per point with the same arithmetic as `update_radiator_outputs()`
(`rated / (50 / ΔT)^1.3`, ΔT being mean water temperature against room air), including the rule
that it is zero unless the heat pump was heating: `isHeatingAt()` tests each point's timestamp
against the same heating spans the mode strip draws, so the line is zero for up to a bucket at each
end of a run, and zero all day where no mode was recorded. That needs the rated
output at dT 50, which the room payload does not carry, so the page fetches `GET /api/radiators`
once for the whole set, and the room's own series doubles as the room-temperature lookup. The join
is **on the timestamp, not the array index**: all three kinds share one sampling interval, but
`stride` is computed per file from that file's slot count, so a radiator whose file started mid-day
buckets differently from the room's. Keep any new derivation on this side of the wire — see the note
on `history_api.c` above.
- **`react-use-websocket`** via `WSContext.jsx` for real-time data updates from the device
- **Bootstrap** (icons) for styling

There was a **Thread Network** tab that drew the mesh topology with `vis-network`. It is gone, and
with it `vis-network`, `vis-data`, `POST /api/network` (`network_post_handler`, which read the
`NeighborTable` attribute from every non-ICD node), the `NeighborTable` branch of
`attribute_data_cb`, and the `network` WebSocket channel.

`ThreadNetworkDiagnostics::ExtAddress` is **not** part of that removal — it is subscribed for every
node, stored on `matter_node_t::ext_address`, persisted in the node NVS blob and reported by
`GET /api/nodes` as `extAddress`.

Routes mirror the REST API structure: `/rooms`, `/rooms/:roomId`, `/radiators`, `/radiators/:radiatorId`, `/devices`, `/devices/:nodeId`, etc.

`/layout` (`Layout.tsx`) is the exception — it is a view over the whole system rather than one
resource. It draws the circuit as a schematic: a flow trunk across the top, a return trunk across
the bottom, and one radiator symbol dropped between them per emitter, with the heat meter on the
left. Geometry comes from a supplied mock-up and is laid out from the radiator count, so the canvas
widens as the system grows and scrolls horizontally rather than shrinking.

It is the reason `GET /api/rooms` carries each room's `radiators` inline. There is otherwise **no
pair of calls that can be joined**: `GET /api/radiators` does not report a room, and
`radiator_t::room_id` is declared and persisted but never assigned, so `room->radiators[]` is the
only real link. The page draws one column per radiator, so a room with several gets a bracketed
group rather than one aggregated symbol.

Live values come from the existing WebSocket channels, not from polling — `home` for the heat
meter, `radiator` and `room` for the columns. That is why the `radiator` push carries
`currentOutput` alongside the temperature that changed: the schematic shows watts per radiator and
only ever sees that channel, so without it the figure would sit stale until a reload. It is added
**after** `update_radiator_outputs()` runs, or it would carry the previous reading.

`Layout.css` scopes every rule under `.layout-schematic`. The mock-up styles bare `.legend`,
`.canvas`, `.name`, `.meter` and `.alert` — unscoped, that last one would restyle every Bootstrap
alert in the app. It needs no entry in `WEB_APP_FILES` because Vite merges all imported CSS into
the single `app.css`.

The built output (`html_compiled_app/index.html`, `app.css`, `app.js`) is embedded directly into the firmware binary — no separate file system is used — and served from the `_binary_*_start`/`_binary_*_end` symbols by `wildcard_get_handler` in `app_main.cpp`, which falls back to `index.html` for the SPA's client-side routes.

Because the files are listed explicitly in `main/CMakeLists.txt`, Vite must keep emitting exactly `index.html`, `app.css` and `app.js` (no content hashing — see `rollupOptions.output` in `html_app/vite.config.ts`). Adding a fourth asset means adding it to `WEB_APP_FILES` and giving it a handler.

### Key Configuration Files

- `sdkconfig` — ESP-IDF Kconfig settings (target chip, partition layout, Matter settings)
- `main/matter_project_config.h` — Matter-specific compile-time config
- `main/esp_ot_config.h` — OpenThread configuration
- `main/linker.lf` — Linker fragment (memory placement)
