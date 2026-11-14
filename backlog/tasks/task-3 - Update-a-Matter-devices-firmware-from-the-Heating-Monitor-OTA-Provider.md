---
id: TASK-3
title: Update a Matter device's firmware from the Heating Monitor (OTA Provider)
status: In Progress
assignee:
  - '@claude'
created_date: '2026-09-12 05:35'
updated_date: '2026-11-09 18:31'
labels:
  - firmware
  - matter
  - web-ui
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The user wants to update the firmware of Matter devices the monitor has commissioned (first: the Kronoterm ADAPT Modbus adapter, ~/development/matter-esp32-modbus-adapter, which has the Matter OTA Requestor enabled and two OTA app slots) from the monitor itself, without USB and without a separate Linux OTA provider.

Matter OTA is pulled by the device: the device invokes QueryImage on an OTA Provider node on its fabric, then downloads the image over BDX. The monitor is the commissioner and admin of its fabric, so it can both announce itself (AnnounceOTAProvider) and act as the Provider.

Findings that shape the approach:
- The monitor is a controller-only build (CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=n), but esp-matter's matter_controller_client already runs a CASE server and operational DNS-SD (enableServerInteractions defaults on), so devices can open sessions to it.
- esp-matter's controller has an OTA Provider path behind CONFIG_CHIP_DEVICE_ENABLE_DYNAMIC_SERVER + CONFIG_ESP_MATTER_OTA_PROVIDER_ENABLED: its data model provider serves the OTA Software Update Provider cluster on endpoint 0, and CHIP's dynamic_server AccessControl grants Operate on that cluster only, to any CASE peer. No esp-matter example uses this combination.
- The delegate it wires in (EspOtaProvider) only serves images listed on the CSA DCL, fetched over HTTPS. The adapter uses test vendor 0xFFF1, which is not on the DCL, so the monitor needs its own delegate and BDX sender serving a local file. The controller data model exposes set_ota_provider_delegate() for this.

Images live on the SD card. An update is only served when the user asked for it for that device: a device that queries on its own schedule is told nothing is available.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The user can upload a Matter .ota image from the web UI; it is stored on the SD card and listed with its vendor ID, product ID, software version and version string read from the image header
- [ ] #2 A file that is not a valid Matter OTA image is rejected on upload with a clear message and nothing is left on the card
- [ ] #3 The user can delete an uploaded image
- [ ] #4 From a device's page the user can start an update with an uploaded image; the monitor then announces itself to that device as its OTA Provider
- [ ] #5 The monitor serves the image only to the device the user started the update for, only when the image's vendor and product IDs match what the device reports and its version is newer; otherwise the device is told no update is available and the UI says why
- [ ] #6 A device querying for updates on its own, without the user starting one, is told no update is available
- [ ] #7 The device page shows the update's progress (waiting for the device, transferring with bytes sent, applying, done or failed) and the version the device reports afterwards
- [ ] #8 Only one transfer runs at a time; a second request while one is running is refused with a clear message
- [ ] #9 Serving an update does not stall the CHIP task (no SD card reads beyond one BDX block per message on it) and does not restart the board
- [ ] #10 With no SD card fitted, the OTA page says so and nothing else is affected
- [ ] #11 Existing subscriptions, commissioning and the rest of the web UI keep working with the provider enabled
- [ ] #12 firmware/CLAUDE.md documents the provider, the Kconfig it needs, the API, and how to build a .ota image for a device
- [ ] #13 Verified end to end by updating the Kronoterm adapter from the monitor, or the remaining unverified steps are listed in the task notes
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Kconfig (sdkconfig AND sdkconfig.defaults, since sdkconfig is checked in): CONFIG_CHIP_DEVICE_ENABLE_DYNAMIC_SERVER=y, CONFIG_ESP_MATTER_OTA_PROVIDER_ENABLED=y, CONFIG_ESP_MATTER_OTA_CANDIDATES_UPDATE_PERIODICALLY=n (no DCL polling). Build first with nothing else changed to confirm the untested controller + dynamic-server combination compiles and the board still commissions and subscribes.
2. main/ota/ota_image_store.{h,cpp}: images in /sdcard/ota/. Upload streams the request body to a temp file, parses the header with CHIP's OTAImageHeaderParser, renames to <vid>-<pid>-<version>.ota on success, deletes on failure. List/delete.
3. main/ota/local_ota_provider.{h,cpp}: an OTAProviderDelegate (QueryImage / ApplyUpdateRequest / NotifyUpdateApplied) plus a bdx::Responder that reads the chosen file one block per query. State per update: node id, image, status, bytes sent, error. Serves only the node the user armed, only on VID/PID match and newer version. Installed after matter_controller_client::init() via data_model::provider::set_ota_provider_delegate(), and takes over the BDX unsolicited handler from EspOtaProvider (unregister, register ours).
4. main/commands/announce_ota_provider_command.{h,cpp}: AnnounceOTAProvider to the device's endpoint 0 (providerNodeID = the monitor's node id, vendor id, reason UpdateAvailable, endpoint 0), following the identify_command pattern.
5. HTTP API: GET /api/ota/images, POST /api/ota/images (raw body upload), DELETE /api/ota/images/{name}, POST /api/nodes/{id}/actions ota-update {image}, GET /api/ota/status. Check max_uri_handlers (currently 44) has room.
6. Web UI: an OTA images section (upload, list, delete) on Settings or its own page, and an Update firmware card on Device.tsx with progress polling.
7. Docs in firmware/CLAUDE.md, plus the adapter-side steps (CONFIG_CHIP_OTA_IMAGE_BUILD, bump PROJECT_VER/PROJECT_VER_NUMBER).
8. Test on the board: flash (USB, with the user's go-ahead), upload an adapter image, run the update, confirm the adapter reports the new version.
<!-- SECTION:PLAN:END -->
