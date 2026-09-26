---
id: TASK-6
title: Open a device's commissioning window from the Device page
status: Done
assignee: []
created_date: '2026-09-17 05:14'
updated_date: '2026-09-25 05:27'
labels:
  - firmware
  - web-app
dependencies: []
ordinal: 3000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Let a user share a commissioned device with another Matter controller. A button on the device details page opens an enhanced commissioning window on the device and shows a new page with the setup code as text and as a QR code.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Device details page has a section with a button to open a commissioning window
- [x] #2 PUT /api/nodes/:nodeId/commissioning-window opens an enhanced window and returns the manual code and QR payload, or a meaningful error status
- [x] #3 A new page shows the setup code as a string and as a QR code, with the window's expiry
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented and builds (firmware + web app); not yet tested on hardware against a real second controller.
- main/commands/commissioning_window_command.{h,cpp}: wraps CHIP's CommissioningWindowOpener (enhanced window, 900 s, random passcode/discriminator, reads VID/PID). One opener per request, freed in its callback, so an abandoned request can't wedge later ones. Blocks the httpd task up to 30 s.
- PUT /api/nodes/:nodeId/commissioning-window → {manualCode, qrCode, timeout}; 409/504/502 on failure.
- Device.tsx "Share" section; CommissioningWindow.tsx page at /devices/:nodeId/commissioning shows QR, formatted manual code and a countdown. Code is passed in history state so a reload doesn't reopen the window.
<!-- SECTION:NOTES:END -->
