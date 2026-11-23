---
id: TASK-25.1
title: emoncms posts fail on stale keep-alive connections
status: In Progress
assignee:
  - '@claude'
created_date: '2026-11-23 12:11'
updated_date: '2026-11-23 13:05'
labels:
  - emoncms
  - firmware
dependencies: []
references:
  - firmware/main/emoncms/emoncms_uploader.cpp
parent_task_id: TASK-25
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
emoncms.org (Apache) closes idle keep-alive connections after 5 s (`Keep-Alive: timeout=5`, measured 2026-11-23). The uploader keeps its HTTPS client open between posts, so at the 10 s interval every post that follows 10 s of idle goes out on a connection the server has closed, and fails with ESP_ERR_HTTP_FETCH_HEADER, logged as "Post failed (HTTP 0)". The reading is retried 5 s later on a new connection and succeeds, so nothing is dropped, but on a live board one post in three failed (862 ok / 431 failed / 0 dropped over 2.4 h), readings arrive 5 s late, the posts_failed counter is meaningless, and the warnings hide real outages. At 30 s or longer intervals every post would fail first time.

Outcome: a post that fails only because a reused connection had gone stale is delivered on the same attempt, and only real failures are counted and logged.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 A post that fails at the transport level on a reused keep-alive connection is retried once, immediately, on a new connection, and counts as a single success if that succeeds
- [ ] #2 A failure on a freshly opened connection is not retried immediately; it goes through the existing buffer and backoff path unchanged
- [ ] #3 A timeout is not retried immediately, so a dead server does not hold the task for twice the HTTP timeout
- [x] #4 Connection reuse is kept for backlog posts sent 250 ms apart
- [x] #5 The 'Post failed' warning names the transport error (e.g. ESP_ERR_HTTP_FETCH_HEADER) when there is no HTTP status, and still never logs the response body or API key
- [ ] #6 Firmware builds; on the board posts_failed stays at 0 over at least 5 minutes at the 10 s interval with emoncms.org reachable
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
Approved by the user in session on 2026-11-23.

1. emoncms_uploader.cpp post_payload(): note whether s_client already existed (reused keep-alive connection) before sending.
2. If esp_http_client_perform() fails and the connection was reused and the error is not ESP_ERR_HTTP_EAGAIN (IDF's read timeout), close the client and send once more on a fresh connection. Stale-connection failures in IDF 5.5.4 surface as ESP_ERR_HTTP_FETCH_HEADER / CONNECTION_CLOSED / WRITE_DATA. The server closed the socket before the request arrived, so the retry cannot double-post (and a duplicate with the same time would only overwrite).
3. Keep keep_alive_enable so backfill (250 ms pace) reuses the connection.
4. post_and_record(): when http_status is 0, log the transport error name (result->response holds esp_err_to_name in that case); otherwise keep logging only the status. Never log the body or key.
5. Build with IDF v5.5.4 + esp-matter export; flash only with user's OK; verify posts_failed via /api/emoncms/status.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-11-23: Implemented in emoncms_uploader.cpp. post_payload() now builds the body and calls a new send_post() (one attempt; returns the transport error and leaves the connection closed on one). If the connection was reused and the error is not ESP_ERR_HTTP_EAGAIN, it calls send_post() once more on a fresh connection; post_and_record() only sees the final result, so a stale-connection recovery counts as one success. Timeout mapping checked in IDF v5.5.4 esp_http_client.c: esp_http_client_fetch_headers() returns -ESP_ERR_HTTP_EAGAIN on ERR_TCP_TRANSPORT_CONNECTION_TIMEOUT and perform() passes it through; FIN gives CONNECTION_CLOSED, other read failures FETCH_HEADER. post_and_record() logs the transport error name when http_status is 0 (response then holds esp_err_to_name or a fixed message, never a body). firmware/CLAUDE.md emoncms section updated. Build green. Not yet flashed: the board runs 0.0.6-100-g49351e8-dirty, so flashing this build also brings commit 00c3854.

2026-11-23: Flashed as part of 0.0.6-101-g00c3854-dirty. On the board at 10 s interval: 21 posts OK, 0 failed, 0 dropped over the first ~3.5 min of posting (before the fix about 1 in 3 failed, so ~7 failures would be expected). AC6 asks for 5 minutes; recheck posts_failed later before closing.
<!-- SECTION:NOTES:END -->
