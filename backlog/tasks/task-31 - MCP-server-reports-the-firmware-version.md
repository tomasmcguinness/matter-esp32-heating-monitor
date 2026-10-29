---
id: TASK-31
title: MCP server reports the firmware version
status: In Progress
assignee:
  - '@claude'
created_date: '2026-10-29 15:46'
updated_date: '2026-10-29 15:53'
labels:
  - firmware
  - mcp
  - release
dependencies: []
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The MCP server's initialize response reports serverInfo.version as a hardcoded "0.1.0" (MCP_SERVER_VERSION in firmware/main/mcp/mcp_server.c). For a release it should report the actual firmware version (esp_app_get_description()->version), so clients and bug reports show which build is running. mcp_server.c is also built on the host for tests, so the version must be passed in rather than read via ESP-only APIs inside it.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 MCP initialize reports the firmware's app version as serverInfo.version
- [x] #2 No hardcoded MCP server version remains
- [x] #3 Host test asserts serverInfo.version equals the version supplied at registration, and test_host/run.sh passes
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved plan (2026-10-29): mcp_server_register() takes the version string (alongside the local zone from TASK-30); app_main passes esp_app_get_description()->version; remove MCP_SERVER_VERSION and build serverInfo from the stored copy; host test asserts the version round-trips.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-10-29: mcp_server_register(server, local_zone, version); app_main passes esp_app_get_description()->version. MCP_SERVER_VERSION removed. Host test asserts serverInfo.version round-trips. AC #1 awaits an on-device initialize call.
<!-- SECTION:NOTES:END -->
