# Inherited floor, scale and comfort view adapter

This P1 increment reuses independently portable view behavior from product
`1327f795cc2e3a8e4f7c9d68e31d64383930cc00`. It does not complete the inherited
aiming, roomscale, controller, weapon, avatar or network integration.

## Source evidence and decision

Product `Quake/vr.c:1897` defines metres-to-units as
`vr_world_scale / (1.5 * 0.0254)`. Its settings at lines 1906 and 1926–1927
default to `vr_viewkick=0`, `vr_world_scale=1`, and `vr_floor_offset=-16`.
The floor offset is in Quake units, added after conversion; eye offsets at
lines 10301–10309 are relative to the player body. Product `view.c:826` replaces
desktop viewheight with this VR offset rather than adding both heights.

The view comfort hooks independently disable locomotion bob, movement roll,
pitch drift, death roll and desktop view-offset clamping. Damage-kick generation
and both gun-kick modes obey `vr_viewkick`. Idle motion and stair smoothing stay
enabled in the source and are retained here. The source's effective floor helper
also includes Gorilla posture policy; that coupled behavior remains in its later
movement port rather than being approximated here.

The minimal adapter retains `view.c` as the camera/comfort owner and the donor
stereo preparer as the renderer owner. It adds no new service, allocator, task
graph, input clock or gameplay state machine. The saved view base remembers its
original desktop viewheight so a newly received stat cannot corrupt a paused
base. The renderer replaces that contribution with absolute tracked height only
for a floor-known player view, preserving node bias and stair correction.

The existing OpenXR owner publishes one fact: whether the selected application
space is floor-referenced. `STAGE` and `LOCAL_FLOOR` provide a floor origin;
plain `LOCAL` does not guarantee one. This follows the official
[OpenXR reference-space definitions](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrReferenceSpaceType.html)
and [local-floor contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR.html).
Runtime selection is unchanged. The `LOCAL` adapter retains relative vertical
tracking at the default floor offset; changes to that setting adjust the
relative baseline. It does not claim a calibrated physical floor in `LOCAL`.

## Review disposition

Astra (`gpt-6-astra`, explicitly `xhigh`) audited the original head/view path and
the independent slice. Main verified effective model/effort through a narrow
read-only metadata query. Terra implemented only backend floor metadata and its
existing fixture; main owns view/render integration and consolidated checks.

| Recommendation | Disposition |
|---|---|
| Reuse scale/floor/comfort independently; do not import the entire coupled `VR_UpdateScreenContent`. | **Adopted.** Existing view, frame and renderer owners receive the settings/formula/hooks. The source roomscale consumer and command semantics remain required future work. |
| Distinguish runtime floor space from `LOCAL`; never add tracked height on top of desktop viewheight. | **Adopted.** The backend publishes selected-space metadata; the view adapter subtracts the height belonging to its saved base before adding floor offset and physical head height. |
| Rotate physical translation through yaw only. | **Adopted for head displacement.** Desktop pitch/kick no longer tilt physical head height. Current camera-orientation and eye-relative composition remain together until coherent aiming integration; no full pose parity is claimed. |
| Preserve idle and stair behavior; they are not disabled by the inherited comfort hooks. | **Adopted.** Those donor functions remain in their original path. |
| Horizontal cancellation cannot be lifted without the existing roomscale accumulation/authority consumer. | **Adopted.** The temporary horizontal anchor remains explicitly incomplete. No substitute movement protocol is added. |
| A completed chase-camera base is no longer a player-eye base; adding absolute floor height after its collision trace can lift it through a ceiling. | **Adopted.** Main verified the donor chase override. The saved base is classified after preparation, and chase/intermission remain camera-relative. Paused setting changes do not reclassify an already-prepared base. |
| Gating recoil interpolation freezes an old recoil accumulator while the server state recovers. | **Adopted.** The existing lerped recoil continues updating; only its contribution to the view is suppressed. Focused checks cover suppression, server recovery and re-enabling. Damage feedback timing also continues to decay. |

Main review corrected a fixture setup mistake: successful frame publication
requires queue callbacks to be registered before the simulated live session is
installed. The fixture now checks the real `VRXR_BeginFrame` publication path for
all three selected reference-space types. It does not prove runtime selection
fallback or actual tracking on a headset.

Astra's focused final source inspection closed both the chase-floor and stale
recoil findings after these fixes. The corrected fixture then passed the added
regressions. The GPU check remains main's integration evidence rather than an
independent reviewer-run test.

## Consolidated local evidence

- Linux SDL3 debug-symbol build passed.
- Production view/camera fixture passed floor height, crouching, scale/IPD,
  pitched basis, paused changed-viewheight, `LOCAL` fallback, invalid scale,
  desktop/VR bob/roll and both gun-kick modes. Existing camera restoration,
  reference invalidation and abort boundary checks still pass.
- OpenXR Vulkan boundary fixture passed, including new reference-space metadata.
- Live isolated Monado/RTX 4090 smoke passed 19 probes over 1,586 scene frames,
  including twelve effective transparency/MSAA/indirect combinations, resize,
  scale changes, floor changes while paused, recovery from zero worldscale,
  the real donor chase camera at scale 2 and return to first person.
  Prepared head height matched the inherited body-plus-height formula, and
  eye separation matched runtime pose distance times worldscale. Normal exit;
  no Vulkan validation errors or synchronization hazards reported.
- Initial, scaled, raised and chase compositor images were inspected. Both eyes
  contain full scene output; raising scale/floor changes the view as expected.

One expanded run hit its 55-second harness timeout after passing the chase
probe, before the final first-person return. It is not counted as a completed
run. After confirming that process had ended, the same check completed normally
with a 90-second budget. The test service was then stopped.

The live test is a stock `start` renderer/view check with a simulated device.
It is not a performance comparison or complete geometric/gameplay acceptance.
Head/hand aiming, inherited recenter/server-yaw semantics, weapon placement,
roomscale authority, independent avatar/shadow poses and headset testing remain
open. In particular, the donor weapon presentation is not yet a tracked weapon
and can fall below the view at increased worldscale. Windows and ARM verification
remain deferred until the end.
