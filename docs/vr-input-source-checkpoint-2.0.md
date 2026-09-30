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
