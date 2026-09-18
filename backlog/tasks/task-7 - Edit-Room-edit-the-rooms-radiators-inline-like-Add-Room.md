---
id: TASK-7
title: 'Edit Room: edit the room''s radiators inline, like Add Room'
status: In Progress
assignee: []
created_date: '2026-09-17 05:40'
updated_date: '2026-09-17 05:42'
labels:
  - web-app
dependencies: []
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Edit Room only has a checkbox list of radiators that is never pre-checked, so saving wipes the room's radiators. Replace it with the same emitter cards Add Room uses, pre-filled from the room's radiators.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Edit Room shows a pre-filled emitter card for each radiator in the room
- [x] #2 Emitters can be added, edited and removed; removed radiators are deleted on save
- [x] #3 Saving without touching emitters keeps the room's radiators
- [x] #4 Add Room behaves as before, sharing the emitter card component
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented; type-checks, lints clean on new/rewritten files, firmware builds with the new UI embedded. Not yet exercised on hardware.
- New EmitterCard.tsx (card UI) and emitter.ts (Emitter type + emitterToRadiatorJson), used by AddRoom and EditRoom.
- EditRoom loads GET /api/rooms/:id then GET /api/radiators/:id per radiator. Save: PUT existing / POST new radiators, PUT room with the IDs, then DELETE radiators removed from the form.
- Room MQTT name still not editable (room API doesn't carry it).
<!-- SECTION:NOTES:END -->
