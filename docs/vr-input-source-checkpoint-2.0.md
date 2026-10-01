# Inherited input and haptics source checkpoint

2026-09-30. Primary51b452c0 and current2.0 sources inspected directly. This is
source evidence, not software or headset acceptance. No builds/tests ran.

## Turning and physical-hand ownership

Primary vr.c:11743–11791 uses one snap per signed-axis transition, otherwise
axis times frame duration times100 times yaw multiplier/turn speed, and a
separate enabled180 command. Current vr_input.c:4914–4968 preserves that
calculation/defaults through finite admission and V_TurnTrackedYaw. Its180
command3610 queues only during valid gameplay, is consumed once at Move entry,
and does not wait in menus for a later connection. view.c:1559 keeps the same
local yaw owner while rejecting pending authoritative alignment, reference
changes and invalid poses. Movement directions are resolved after the turn.

The current physical-to-logical hand mapping424 and its inverse share the
lefthanded setting. Commands4071–4110 detects role/profile changes, releases
keys already owned by that physical hand and requires neutral before rearming.
Release3632 respects a key also held by the other hand and checks dispatch/reset
changes after native Key_Event. Focus/inactive hands take the same release
boundary. Motion setting changes reset pending roomscale/turn/gesture continuity
through5153–5170; no parallel turn or key state owner was added. These are source
checks of VR-006/007, not proof of actual controller behavior.

## Verified missing paired sound haptic and before-code adapter

Primary cl_parse.c:388–399 gives every eligible local interaction its existing
dominant pulse and adds an off-hand pulse for a resident active pair. It excludes
remote/world/monster sounds and explicit footsteps/pain/burn/drown samples.
Current cl_parse.c:1765 retains that classification and dominant pulse but
explicitly omits the pair query as not migrated. That comment is stale:
view.c:1349 now implements V_AkimboPairReady at the existing selection/geometry
owner, without model loading. It checks current model/stat/recipe/animation,
resident source/half identities, both tracked devices and prepared transforms.

Reuse that query at the same existing sound boundary and add the inherited
logical-left pulse only on success. Ownership only cl_parse.c; expected two
net production lines. Compare adding another residency/haptics model registry:
it duplicates existing selection/cache policy and risks loading from packet
parsing. No new query/state/cache or renderer adaptation is needed.

CL_ReadFromServer runs before the new GL_BeginRendering/view preparation. The
existing last prepared pair is therefore the available residency snapshot; the
query must not prepare a new pair or relax the renderer's frame/identity guards.
Transitions that have no valid prepared pair keep the ordinary dominant pulse.
Packet bad-read/sound bounds/precache admission remains unchanged.

VR_InputTriggerHaptic438 maps each logical pulse back to its physical hand and
checks the shared user toggle/XR attachment. VRXR_Haptic1395 requires focused
running session, finite duration/amplitude and valid physical hand, then uses
the existing OpenXR haptic action with duration/amplitude bounds. Existing wheel
selection2923 and menu navigation3856 share that same owner. No per-eye pulse or
physical-contact damage feature is introduced.

## Final software acceptance

After implementation, Linux/ARM checks cover signed snap transitions, smooth
frame duration,180 enable/disable/menu/focus behavior, left-hand role remap and
release, shared keys/profile changes and authoritative camera discontinuities.
For haptics check ordinary versus ready paired models, unavailable/stale pair,
local versus remote sounds, excluded locomotion/damage samples, toggle, focus
loss and wheel/navigation. Actual headset haptics remain user live testing.

## Paired haptic source integration

The cl_parse.c adapter adds the inherited logical-left pulse through the
existing V_AkimboPairReady predicate and replaces the stale unmigrated comment
(three additions/two deletions, one net line). Main compared the primary local
sound filter/pulse and the native predicate/GL frame lifetime. The predicate
performs no model load; ordinary dominant feedback and sound admission remain.
Scoped git diff --check passes; execution remains deferred.

## Current profile and inherited axis source reconciliation (2026-09-30)

Requested local Astra xhigh's bounded primary/current comparison identified
two omissions beyond the historical mapping table: gameplay weapon-hand
horizontal arrow events and Vive rising-pad-click sector cycling. Main traced
the primary producer and current axis/key/command owners and adopted the
[before-code minimal adapter](controller-axis-parity-2.0-plan.md), including
the review's entry-time calibration/wheel consumption snapshot correction.
Luna implemented production786ca872 (23 added lines/one replacement); main
reviewed the complete diff and changed the snapshot type to native qboolean.
Final requested-Astra review of the committed patch reported no P1/P2 findings;
effective routing metadata was unavailable, so this is bounded source advice.
Existing aggregate/per-hand ownership carries the press edge; no new persistent
controller state or native command dispatcher is added. Broader controller
and final Linux/ARM software qualification remain pending.

The existing backend's profile_for_hand queries each physical hand independently;
input_for_hand projects active actions into one completed sample. Frame native
extension/profile paths and Touch compatibility match fresh official
[Valve input documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/input).
Keep current scalar-to-boolean squeeze projection, simple/Touch/Index/Vive/Frame
binding arrays, logical-role mapping and focused-session haptic owner. Optional
button availability alone does not establish a missing gameplay contract.
This is source integration and documentation evidence, not executed runtime
binding acceptance, controller/haptic behavior or performance qualification.
