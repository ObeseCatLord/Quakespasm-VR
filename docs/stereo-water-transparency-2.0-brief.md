# Q01 stereo water/transparency decision brief

2026-10-01. Main mostly-worked brief for local Astra xhigh design review. Final
checklist Q01 (XR-011/PERF-021), solo project; preserve native Vulkan owners and
desktop, two-view OpenXR, opaque single-pass stereo and FB/META/KHR selection.
No quad views. No tests/builds/probes/render runs until all implementation ends.

## Verified facts

| Evidence | Actual boundary |
| --- | --- |
| Current source, main read | gl_rmain.c:2345 sorts once by native center/bounds distance and splits alpha lists by entity-center liquid contents. :2465 chooses before/after-world-water groups from center r_viewleaf. r_passes.c:242 keeps native across-alpha/water/alpha order with viewmask3. gl_rmisc.c:114 disables sort under OIT. OIT-off remains supported. |
| Native reuse | vkQuake4bc898 owns the same water partition/sort/pass sequencing. Current R_PrepareStereoFrame:811–968 already publishes actual eye origins, existing frame-lifetime scene/display UBOs and optional shared waterwarp. R_BindPipeline glquake.h:1043 binds set5/dynamic offset every call even for unchanged pipeline. |
| Primary pin51b452c0, main read | vr.c:10648–10699 builds physical eye offsets and calls rendering twice; gl_rmain.c:895–935 queries each eye leaf. :1169 shares one alpha sort origin intentionally. :1964–1974 draws opaque, then world water, then all alpha: it does not have native vkQuake's liquid partition heuristic. Shared alpha sort alone is not a missing feature. |
| Shader ABI, main read | Shaders/stereo.inc has a std140 set5/binding0 block (two mat4 and two vec4,160bytes), gl_ViewIndex, one shared clip-correction macro, eye-offset xyz. Current main publishes that block at gl_rmain.c:954. All participating scene vertex variants reuse the include. Push constants have a native128-byte ceiling; avoid adding mask state to every material push layout. |
| Current mutation/caller limits | R_SetupAliasFrame and R_GetEntityLerpedTransform are observational/const. R_DrawEntitiesOnList:1244 still mutates local nonalias pitch; brush chain slots and conditional model/skin access need consideration if the same alpha entity is recorded at both stages. Do not silently introduce concurrent duplicate entity mutation. |
| Workspace | Writable quakespasm-2.0 only, main/user dirty migration-2.0.md untouched. Physics and particle Luna workers own only sv_phys.c/r_part.c. Source references primary51b452c0, vk4bc898, QSS-M03a498 and Ironwail08d578 are read-only. Assets remain in quakespasm_straight. |

Official contracts read by main2026-10-01:
[Vulkan multiview](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassMultiviewCreateInfo.html)
states that subpass masks broadcast draws/clears and gl_ViewIndex selects each
attachment layer; masks may be single-view but all subpasses of a multiview pass
remain nonzero. Correlation hints do not change results.
[Vertex post-processing](https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html)
places primitive clipping before perspective division/rasterization. A finite
constant clip position outside the x half-space is a candidate per-view exclusion
without fragile depth-clamp assumptions. No measured speedup is asserted.

## Main interpretation and open decision

Native center-water partition broadcasts the same order to both views even when
one eye is liquid and the other isn't. This is an actual native heuristic mismatch
to per-eye camera contents, though primary uses a simpler always-water-first
heuristic and does not prove ideal per-pixel transparency itself. Retain native
sorting, shared conservative visibility and common waterwarp/fog policy. Need a
decision on the narrow alpha/water ordering exception, not a full transparency
replacement or perfect intersecting-transparency renderer.

Options:

1. Adopt native head-center liquid ordering as a deliberate VR policy. Smallest
   code cost, but no current source proof shows it preserves the native heuristic
   for opposite-eye liquid contents. Do not close Q on an unsupported claim.
2. **Main lean:** preserve the two-view pass and existing sorted lists. Capture
   each eye's liquid boolean at current immutable frame setup. In each existing
   across-water/same-water alpha command context, select each list's participating
   eye mask. When both eyes agree, draw the existing group once for both views.
   When they differ, record both lists with left-only/right-only scene UBOs;
   existing world-water work still broadcasts once. Extend the existing stereo
   block with an explicit uint mask/padding (176bytes) and shared clip macro that
   sends inactive views to finite clip vec4(2,0,0,1). No new pass/frame clock,
   renderer, pose cache or material-push policy. Allocate the two masked descriptor
   payloads only on disagreement through existing R_UniformAllocate. Use per-cb
   override/mask state at existing R_BindPipeline; retain display/UI projection.
3. Single-eye render-pass/subpass exceptions around all transparent work. Vulkan
   permits masks1/2 but this adds compatibility variants, sequencing and possibly
   attachment-load transitions to native MSAA/OIT/foveation topology. Reject unless
   narrower descriptor adaptation is unsound. Full second engine render would
   duplicate mutable simulation/preparation and is not justified.

Reject forced OIT, independently sorting all entities twice and singular-matrix
clipping hacks. They either remove native supported options, add unnecessary
common-frame CPU work or make exclusion hard to reason about. Full-rate/foveated
scene transforms stay the existing owner's responsibility.

## Necessary constraints for option2

Mask semantics must give each eye exactly its native liquid-category sequence:
across-water alpha group before world water, matching group afterward. With
same eye contents, use one group per stage; eye contents can both differ from
center, so don't special-case only center disagreement. If alpha sorting is off
or OIT on, retain native existing draw path. If no valid two-eye world frame,
retain native fallback. Never mask world/depth/particles/HUD/SSAO or carry a
context override into unrelated draws. Each stereo UBO publisher initializes the
new field. Nonparticipant clipping uses x, not depth, and no fragment-depth side
effect. State reset uses existing context/frame retirement.

Explicitly challenge duplicate recording: existing alpha stage tasks are
independent; masked two-list recording must not race native skin/model/chain
state or multiply local nonalias pitch. Prefer shared observational helpers or
serialize just the exceptional recording tasks through the existing task DAG,
whichever is actually necessary. Do not introduce an entity-preparation service
or change native desktop chase behavior to accommodate an unproven hazard.

Estimate100–160 production lines across gl_rmain.c/glquake.h/gl_rmisc.c and
stereo.inc, retaining existing topology. Reopen if new state owners/pass variants
or significant adjacent refactors become necessary. The smallest eventual
vertical proof is rendered two-layer alpha/liquid ordering with opposite-eye
contents, same contents (including center mismatch), native sort/OIT toggles,
MSAA/AO/foveation on/off, and no duplicate gameplay/pose updates. All executable
qualification remains end-of-implementation; source review does not prove images.

## Requested review contract

Read-only local Astra xhigh. Verify this brief's load-bearing actual source and
official primary contracts, then choose/rank minimal design or exact missing
evidence. Output<=1400words: decision, necessary changes, simplification/reuse,
mutation/ABI/frame/DAG hazards, smallest final proof and explicit unknowns.
No edits/tests/builds/probes/fixtures/game/telemetry/nested agents. If too broad,
state exact missing evidence without replacing adjacent working systems.
