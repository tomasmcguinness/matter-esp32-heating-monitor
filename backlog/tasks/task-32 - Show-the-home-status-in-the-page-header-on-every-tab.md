---
id: TASK-32
title: Show the home status in the page header on every tab
status: In Progress
assignee:
  - '@claude'
created_date: '2026-10-29 16:28'
updated_date: '2026-10-29 16:30'
labels:
  - web-ui
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The outdoor temperature, average indoor temperature and heat pump mode (HomeStatus) only appear on the Home page, inline after the title. The user wants them centred in the page header, between the page title and the page's action button, and visible on every tab so the current state is always in view. The live values come from GET /api/home and the "home" WebSocket channel; WSContext keeps one callback per channel, so the header and the Home page cannot both subscribe to it directly.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Every top-level tab (Home, Layout, Rooms, Radiators, Devices, History, Settings, Status, Diagnostics) shows the status centred between its title and its action button, if any
- [ ] #2 The status updates live on every tab from the home WebSocket channel
- [ ] #3 Visiting and leaving the Home page does not stop the header updating
- [ ] #4 On narrow screens the header wraps without horizontal scrolling
- [x] #5 The web app type-checks and builds
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. HomeContext.tsx: HomeProvider (inside WebSocketProvider in App.tsx) fetches /api/home once and is the only subscriber to the "home" channel; useHome() returns the latest payload.
2. Home.tsx: drop its own fetch/subscribe; apply the payload from useHome().
3. PageHeader.tsx: h1 as a 3-column grid (title | HomeStatus | actions), centring the status; wraps to two rows below md. Styles in App.css.
4. Use PageHeader on the nine tab pages, moving their action buttons (Edit, Add Room, Add Device) into it.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-29: Implemented as planned. HomeContext.tsx is now the only "home" subscriber; Home.tsx and Layout.tsx (which also subscribed to "home" and fetched /api/home) read from useHome(). PageHeader.tsx (title | status | actions, CSS grid in App.css, status wraps to row 2 below md) used on all nine tabs; it renders the <hr /> too. Sub-pages (room/radiator/device detail, add/edit forms) keep their own headings. tsc, eslint on new files and vite build pass. Not yet checked in a browser against the board: ACs #1-#4 need a firmware build + flash, or the dev server pointed at the board.
<!-- SECTION:NOTES:END -->
