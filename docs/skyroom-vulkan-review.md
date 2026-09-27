# Skyroom on the vkQuake renderer: Astra senior review

Date: 2026-09-26. Branch: `2.0`. The inherited `master` skyroom is the
behavioral reference. This is an implementation decision, not a claim that
skyroom has shipped on this branch.

## Decision

Keep vkQuake's renderer, task graph, multiview path, and server visibility
machinery. Build one additional **view** within the existing logical frame.
Separate frame-wide animation/effects from view preparation, and preserve each
view's visibility and GPU inputs until its commands are recorded and consumed.
Only then choose the render attachment strategy. First attempt the inherited
color-preserving skyroom prepass within the donor pass graph; use offscreen
color/depth plus composition only if the prepass fails a concrete MSAA,
transparency, SSAO, or sky-mask invariant.

The transformed skyroom camera is `configured origin + parallax * incoming
view origin`, with independent rotation. Each eye needs its own transform and
asymmetric projection inside the existing multiview payload. The center camera
alone is insufficient. Main-view depth must contain no skyroom depth; the
skyroom view must not recurse; simulation must advance once per frame.

Server support belongs in the same end-to-end feature. Parse `skyroom` and
`_skyroom` metadata, then add skyroom visibility to both the snapshot and
legacy PVS paths. `SV_FatPVS` clears its shared buffer, so a second ordinary
call cannot simply be ORed after the fact. Preserve the source-compatible
server default until actual client preference signaling and traffic data
justify a per-client bandwidth policy.

## Disposition of review findings

| Finding | Decision | Reason |
| --- | --- | --- |
| Treat rendering and stereo as one view-ownership decision | Adopt | Per-eye view transforms and the scene's GPU data have the same lifetime boundary. |
| Separate frame updates from view preparation first | Adopt | Surface marking and indirect generation overwrite shared state; a second target alone does not fix it. |
| Start with an offscreen compositor | Adapt | Compare the smaller source-style color prepass first. Vulkan/MSAA/OIT/SSAO suitability remains to be demonstrated. |
| Add server skyroom PVS independently | Reject | The source defaults to client rendering off; server-only traffic would buy no visible behavior. |
| Add per-client skyroom preference signaling now | Defer | It adds protocol state before bandwidth requirements are measured. |

## Smallest vertical proof

Use one deterministic map with a sky aperture, foreground occluder, and moving
server entity outside ordinary player PVS. Capture desktop and both VR eyes
with simulated Monado. Compare zero/nonzero parallax, rotation, portal
entry/exit, occlusion, entity visibility, and animation timing against
`master`. The feature is not done when only PVS bits or GPU counters change.
Then qualify sky brush surfaces, map resets, toggle transitions, MSAA, OIT,
SSAO, indirect/task rendering, and both network visibility paths. No separate
quad-view path is part of this work.

## Evidence and ownership

Source reference: `master:Quake/gl_sky.c` (`Sky_SetSkyRoom`,
`Sky_DrawSkyRoom`), `master:Quake/gl_rmain.c` (color-preserving prepass),
`master:Quake/pr_edict.c` (worldspawn parser), and
`master:Quake/sv_main.c` (both PVS paths). Donor constraints:
`Quake/r_world.c` (`R_MarkSurfaces` resets chains), `Quake/r_brush.c`
(indirect command generation), `Quake/gl_refrag.c` (static-entity and effect
frame stamps), `Quake/gl_rmain.c` (`R_RenderView` scheduling),
`Quake/gl_rmisc.c` (sky stencil pipeline), `Quake/r_passes.c` (task ownership),
and `Quake/vr_openxr_math.h` (per-eye clip translation).

The natural implementation boundary is `gl_sky.c` for metadata/masking,
`gl_rmain.c` plus `r_world.c`/`gl_refrag.c` for view staging, `r_passes.c`
plus `r_brush.c` for command and resource lifetimes, and `pr_edict.c` plus
`sv_main.c` for server metadata/visibility. Do not transplant the inherited GL
renderer or add a second scene state machine alongside the donor task graph.

## Follow-up review and implementation boundary (2026-09-27)

The follow-up Astra review kept the same view-ownership design. The smaller
color-preserving prepass remains the first Vulkan experiment; an offscreen
compositor needs a demonstrated attachment or ordering conflict. Server PVS
may land as a separately reviewable preparation commit, but is not skyroom
feature acceptance. Both snapshot writers must extend the already populated
fat PVS: calling `SV_FatPVS` again would clear it.

One extra stencil constraint is now explicit. The OpenXR hidden-area mask uses
stencil bit `0x80`, while the sky pipeline writes `0xff`. A future skyroom
prepass must prove correct depth/stencil reset or preservation, including SSAO,
hidden-depth replay, MSAA, and OIT. It cannot infer safety from color output
alone. AD contains an `info_skyroom` QC path and `_skyroom` field, but no
actual worldspawn activation was verified in its maps. Use a separately
verified map or label a synthetic fixture as such.

The initial preparation on `2.0` parses client/server worldspawn metadata and
adds the server-side skyroom PVS union. It does not render a skyroom view. The
vertical proof and renderer work above remain open.
