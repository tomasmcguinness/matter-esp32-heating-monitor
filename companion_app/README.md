# Heating Monitor Companion App

An iOS app for the ESP32 Heating Monitor. It pairs with the device by scanning a QR code,
lists the Matter devices commissioned onto it, and adds new ones through Apple's system
Matter setup flow.

The phone is **not** the Matter controller — the Heating Monitor is. All this app does is
collect a setup payload and hand it to the device's REST API, which does the commissioning
over BLE and Thread.

## Requirements

- A Mac with Xcode 16 or later
- A **physical iPhone** running iOS 17 or later — `MatterSupport` does not work in the Simulator
- The Heating Monitor on the same network, reachable at `heating-monitor.local`

## Setup

The project is checked in with the original author's team and bundle identifiers. To build
under your own account, change these in Xcode:

| Setting | Where | Current value |
|---|---|---|
| Team | Both targets → Signing & Capabilities | `R9KK4QW233` |
| Bundle identifier | `HeatingMonitor` target | `com.tomasmcguinness.heating-monitor-companion` |
| Bundle identifier | `MatterExtension` target | `…heating-monitor-companion.MatterExtension` |
| App Group | Both `.entitlements` files and `HubStore.appGroup` | `group.com.tomasmcguinness.heating-monitor-companion` |
| Keychain group | Both `.entitlements` files and `HubStore.keychainAccessGroup` | `com.tomasmcguinness.heating-monitor-companion` |

The App Group has to be registered on your developer account, and both targets must be in
it. Without it the extension can't see which hub the app paired with, and commissioning
fails with "This app isn't paired with a Heating Monitor yet."

Then: open `HeatingMonitor.xcodeproj`, pick the `HeatingMonitor` scheme, and run on a device.

## Pairing

1. Browse to `http://heating-monitor.local/settings` on any machine on the network.
2. The page shows a QR code containing the device's address and pairing token.
3. In the app, Devices → **Scan Pairing Code**.

If the camera isn't usable, **Enter Address** on the pairing screen takes a hostname or IP
instead. That path doesn't pick up the token, which is fine today — the firmware logs the
token but doesn't yet reject requests without one.

### QR payload

Produced by `info_get_handler` in `firmware/main/app_main.cpp`, decoded by `PairedHub`:

```json
{
  "v": 1,
  "name": "Heating Monitor",
  "host": "heating-monitor.local",
  "ip": "192.168.1.42",
  "id": "a1b2c3d4",
  "token": "3f9c…"
}
```

`ip` is a fallback for when mDNS doesn't resolve; the client tries `host` first and falls
back to `ip` only on a transport failure.

## Adding a device

Devices tab → **+**. From there everything is Apple's UI: the system sheet opens the camera,
scans the Matter device's QR code (or takes a manual pairing code), and collects Thread
credentials.

When the user confirms, iOS launches `MatterExtension` out of process and calls
`RequestHandler.commissionDevice(in:onboardingPayload:commissioningID:)`. That posts the
payload to the hub's `POST /api/nodes` and stores the returned node id in the App Group.
The app reads that id back and polls `GET /api/nodes` until the device appears — the hub
answers with 202 as soon as it starts pairing, and there's no completion callback on the API.

## Layout

```
HeatingMonitor/          app target
  Pairing/               QR scanner and the pairing sheet
  Devices/               device list, detail, and the + flow
  Settings/              paired hub info, connection test, unpair
Shared/                  compiled into BOTH targets
  PairedHub.swift        the QR payload
  HubStore.swift         App Group + Keychain storage shared with the extension
  Models.swift           API response types
  HeatingMonitorClient.swift
MatterExtension/         Matter device-setup extension target
```

`Shared/` is a member of both targets because the extension is a separate process and needs
the same client and storage. The project uses explicit file references rather than Xcode 16
synchronised folders, so **adding a new source file means adding it to the target in Xcode**
(select the file → File Inspector → Target Membership); dropping it in the folder is not
enough.

## API used

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/info` | Pairing payload; also the reachability check |
| GET | `/api/nodes` | Device list |
| POST | `/api/nodes` | Commission a device (`{"inUse": false, "setupCode": "MT:…"}`) |
| PUT | `/api/nodes/{id}/update` | Rename (`{"name": "…"}`) |
| DELETE | `/api/nodes/{id}` | Unpair |

Every request carries `Authorization: Bearer <token>` when a token is stored. The firmware
currently logs whether it matches but does not reject anything — enforcing it would lock out
the embedded web UI, which has no token.
