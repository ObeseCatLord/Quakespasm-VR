# Weapon-wheel tracking-space anchor reconciliation

2026-09-30. Before-code brief. Solo project: reuse the existing pose transform,
wheel session and prepared stereo frame; do not add another renderer or input
owner. Tests/builds/gameplay remain deferred until implementation is finished.

## Verified evidence and environment

| Artifact | Verified behavior |
| --- | --- |
| Writable checkout | `quakespasm-2.0`, already established 2.0 branch; no branch switching. `docs/migration-2.0.md` is user-owned dirty work and excluded. |
| Primary reference | Read-only `../quakespasm-openvr`, pinned `51b452c018273647dcf94f4628a370267ff8fa91`. |
| Primary `vr.c:VR_PrepareWeaponMenu` | Captures raw tracking position/orientation once, then maps it through `VR_TrackingPointToWorld` and `VR_WeaponMenuCapturedAngles` each logical frame. World/viewentity changes cancel. |
| Primary `VR_TrackingPointToWorld`, `QuatToYawPitchRoll` | Current player origin/floor/mapping yaw participate in remapping the opening tracking pose. The opening pose is not permanently fixed in game-world coordinates. |
| Primary `VR_RunWeaponMenu` | VR view mode uses current `r_refdef.vieworg/viewangles` every frame. Desktop separately captures opening angle while following current origin. |
| Current `gl_screen.c:SCR_VRWeaponMenuPrepare` | Playspace captures world origin/basis once. View mode invokes the generic menu anchor only once, hence freezes it too. Session generation, mode, focus, reference, tracking loss and invalid transform already cancel. |
| Current `gl_rmain.c:R_TrackedControllerBasis` | One existing current-head-relative tracking-to-world transform, using prepared stereo mapping basis, current camera origin and world scale. `R_TrackedControllerRay` reuses it. |
| Current `vr_openxr.cpp:create_actions/sync_actions` | Controller matrices are OpenXR **grip** action poses, not aim poses. `ACT_GRIP_POSE` binds `/input/grip/pose`, and `handSpace` populates frame devices. |
| Current `vr_locomotion.c:VR_LocomotionHandAngles` | Already ports the donor's gun-angle/controller rotation composition. Current held presentation uses it via `V_TrackedPresentationHandAngles`; wheel currently uses raw basis. |
| Current pointer/scene ownership | One preparation before stereo tasks; matching hit/render transform, world occlusion for models and action labels, foreground terminal draw/depth policy already exist. |

All rows above were verified by reading actual handlers/call sites. No execution
or visual acceptance is inferred. Exact old view-mode distance/radius and
controller calibration comfort remain unverified in the current rendered game.

## Decision and minimal adapter

Lean: extract the existing matrix-to-world conversion into a reusable
`R_TrackedPoseBasis`-style helper. Keep physical-hand checks in the controller
wrapper and retain all finite/head/stereo validation. Capture one raw opening
matrix in the wheel's existing session anchor lifetime. Each preparation maps
that same raw matrix through the helper; live controller motion remains the
pointer, not the anchor. Generation/reference/focus/mode/tracking invalidation
continues to cancel and discard the snapshot. No extra yaw/history/pose owner.

View mode should consume current headset view every preparation through the
existing head-follow anchor update, independent of generic menu follow setting.
Retain current panel geometry and renderer ownership in this slice; primary's
different distance/radius does not justify replacing native wheel layout.

Open decision: restore captured `vr_gunangle` orientation composition as part of
this slice using the existing `VR_LocomotionHandAngles`, or isolate it for the
next source delta. Since matrices are grip poses, the presumed aim-pose reason
for omitting calibration is disproven. Avoid a second implementation of the
rotation; establish the same presentation mapping yaw as renderer before reuse.
Live pointer calibration may be the same omission; reviewer may merge it if
evidence proves it, or retain existing ray without broad input changes.

Rejected alternatives: storing a new world/yaw displacement history duplicates
the renderer's mapping policy; moving the whole wheel into a new tracked UI
system duplicates catalog/selection/render ownership; altering general menu
follow modes changes unrelated working behavior. A raw tracking snapshot is
small session data, not a second continuously updated device state machine.

Expected write set: `Quake/gl_rmain.c`, `Quake/glquake.h`, and wheel-only state/
preparation in `Quake/gl_screen.c`. One tightly coupled Luna xhigh worker after
main disposition. Approximately 80–160 lines touched, most extracting existing
conversion code. Reopen if it requires a new calibration/mapping owner.

## Review and final qualification

Requested local Astra xhigh: verify the load-bearing source claims, challenge
the need for the adapter, prioritize actual regressions, settle calibration
reuse and head-follow seam. Read-only, <=1000 words, no whole renderer/catalog/
QC review, no execution. Effective model metadata is not exposed by this tool,
so record this as requested-Astra source advice, not certified skill signoff.

After implementation finishes: actual two-eye wheel opening/motion/selection/
release with body movement, snap/smooth turning, physical head/hand motion,
world-scale changes, both handedness, ring changes, invalid/focus/reference/map/
mode transitions, model and action occlusion, and view mode head follow. Check
draw and hit transforms agree and desktop/no-runtime wheel stays native. Live
headset comfort/performance remain the user's later tests.
