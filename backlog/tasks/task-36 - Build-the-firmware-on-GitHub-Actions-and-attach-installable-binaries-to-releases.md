---
id: TASK-36
title: >-
  Build the firmware on GitHub Actions and attach installable binaries to
  releases
status: In Progress
assignee: []
created_date: '2026-11-05 16:49'
labels:
  - ci
  - release
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A GitHub workflow that compiles the firmware from a clean checkout and, for a version tag, publishes a release with the files a USB (browser) installer needs. Groundwork for the USB installation flow.

The local build depends on state that is not in this repo: ESP-IDF v5.5.4 with the esp-lwip PR 98 ND6 fix as an uncommitted edit, and esp-matter at tomasmcguinness/esp-matter c21bea27 with connectedhomeip moved to c992539e and a one-line edit to the pairing command. CI has to reproduce all of that from pins and patches kept in this repo.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 The workflow builds the firmware on push to main, on pull requests and on demand, and uploads the binaries as a workflow artifact
- [ ] #2 A pushed v* tag creates a GitHub release with the binaries attached
- [ ] #3 The esp-matter, connectedhomeip and lwIP state the local build uses is reproduced from pins and patches committed in this repo
- [ ] #4 Release files include each flash part at its offset, an ESP Web Tools manifest and a single merged image
- [ ] #5 The setup and packaging steps have been run from scratch in the espressif/idf:v5.5.4 container and produce a firmware that links
- [ ] #6 firmware/CLAUDE.md documents the workflow, the pins and how to update them
<!-- AC:END -->
