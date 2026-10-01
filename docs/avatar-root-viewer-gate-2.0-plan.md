# C10/C11 tracked root and viewer eligibility plan

2026-10-01. Before-code plan for final-checklist AV-002 corrections. Reference
primary51b452c0 r_alias.c5961–5975 rotates the tracked render root by the sampled
pose body yaw; vr.c3229 excludes Enyo and uses vr_vrik for tracked posing.
Native desktop graphics/ordinary selected-avatar animation remain authoritative.
All executable qualification is deferred until implementation is finished.

## Verified current seams and adapter

R_VRIKSampleEntityPose in r_alias.c already interpolates body_yaw, but current
R_GetEntityLerpedTransform and R_AliasModelMatrix consumers ignore it. Existing
r_vrik_render.c builds candidates then publishes immutable frame-slot palette
records. The record currently lacks tracked root yaw. Alternate candidate posing
samples even when the viewer's vr_vrik setting is disabled. The existing setting
is static in vr_input.c; its sender-side inactive-sample branch is reusable.

Add finite body yaw plus an explicit valid flag to the existing candidate and
prepared record, captured from the same accepted pose that solves the palette.
No new tracking cache or root state owner; disabled/stale/untracked animation
publishes no tracked root override. Publish these fields with the existing palette
allocation/fence lifetime. Do not resample poses during each draw or mutate
entity/lerp angles. The accepted frame sample must govern body, props and shadows.

Use the existing observational R_GetEntityLerpedTransform root boundary to replace
only yaw when a prepared record has a valid finite tracked root. Native alias
origin, pitch, roll, local-pitch repair, scale and held-viewmodel transforms stay
unchanged. It is already called by visible body, prop and TLAS consumers. Check
alias shading's angle-dependent orientation as part of the same root consistency.
Preparation's muzzle helper runs before the record is published, so apply the
candidate's accepted yaw to its local lerpdata before its existing model-matrix
call; no global publication or shadow-specific transform workaround.

Expose one small read-only VR_InputVRIKAllowed helper at the existing input
interface, combining vr_vrik !=0 and inherited Enyo exclusion. Use it for both
candidate tracked-pose admission and the sender's inactive-sample branch.
Keep alternate-avatar ordinary animation/selection when posing is disabled;
do not return early from the whole alternate renderer. Ranger tracked palette
admission may return false to its existing native animation fallback. Viewer
desktop mode can still display allowed remote tracking. No new cvar or protocol.

## Ownership and stages

Production write set: Quake/r_alias.c, Quake/r_vrik_render.c,
Quake/r_vrik_render.h, Quake/vr_input.c, Quake/vr_input.h. A future single Luna
xhigh worker owns this tightly coupled slice; main reviews/integrates. Do not
overlap current Q01 renderer worker ownership or expand to QBJ3 C12–C14 without
their separate before-code plan. Those later paths must clear root validity for
ordinary deaths/corpses rather than reuse live tracking.

1. Add shared existing-setting/game eligibility getter, then gate tracked sampling
   while preserving alternate native animation.
2. Initialize candidate root fields on every candidate path, set them only with a
   finite accepted tracked palette, and publish them with the prepared record.
3. Adapt the observational root and preparation muzzle consumers. Main inspect
   body/prop/TLAS/showtris/outline paths through the common transform; no parallel
   root implementation or mutable replacement entity.

Expected patch around80–130 lines. Reopen if new pose caches, frame clocks,
avatar rig rewrites or additional production modules become necessary. No
assumption that model/entity yaw updates arrive at the same cadence as tracking.

Final rendered acceptance: delayed/interpolated sampled body yaw versus entity
yaw, selected/default avatars, remote desktop/VR viewers, body/equipment/muzzle/
shadow agreement, conservative bounds, toggle off/on without stale palette roots,
Enyo native fallback, stale/inactive samples and death/disconnect transitions.
Ordinary alternate animation must persist with posing off. Software-controlled
poses and final Linux/ARM qualification provide evidence; source review alone
does not close observable acceptance. No tests/builds/probes/game runs now.
