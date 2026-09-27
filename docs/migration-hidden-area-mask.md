# OpenXR hidden-area mask boundary

`XR_KHR_visibility_mask` can avoid rasterizing pixels outside the headset's
visible lens area. The OpenXR backend already negotiates the optional extension,
refreshes its per-eye mask on the runtime change event, and exports finite,
index-checked, flattened hidden triangles through `VRXR_GetHiddenAreaMesh`.
The vkQuake Vulkan frame recorder now snapshots that mesh for final black
coverage and a restricted opaque-world stencil path. Missing masks leave full
scene quality; no headset-specific mask is assumed. This remains an inherited
VR behavior to finish across the normal rendering modes. The [official OpenXR
extension](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_visibility_mask.html)
defines the optional per-view mask, and the [visibility-mask structure](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrVisibilityMaskKHR.html)
specifies that its XY coordinates lie in the view-space Z=−1 plane and must be
transformed by the projection matrix for each eye. They are not display NDC.

## Architecture boundary

| Candidate | Existing behavior retained | Integration cost and risk |
| --- | --- | --- |
| Draw the hidden mesh at the start of the existing scene pass | vkQuake's multiview pass, task graph, attachments, and OpenXR mesh cache | Needs a mask pipeline, per-eye geometry selection, a compatible depth/stencil policy across SSAO, OIT, MSAA, and density-map variants. |
| New mask framebuffer/frame graph | OpenXR mesh cache only | Duplicates scene ownership and adds extra passes and synchronization; no demonstrated need. |
| Keep the mask disabled during incompatible configurations | All current scene effects and conservative two-eye culling | Does not yet migrate the inherited masking behavior for those configurations. |

The scene uses reversed depth and `GREATER_OR_EQUAL`; writing near depth into
hidden triangles is not a safe general mask because near-plane geometry can
pass equal depth and SSAO reads the same depth image. Stencil is already used
for sky and entity classification, including later stencil clears for SSAO and
co-op outlines. The first scene pass can even choose the no-stencil variant
whose stencil load operation is `DONT_CARE`. Reserving one stencil bit alone
will not survive these passes; a first mask gate should affect only world
drawing, with broader use added after each ownership boundary is qualified.
The frame description in `Quake/r_passes.c` groups world, entity, SSAO,
transparency, and foreground draws; a mask cannot be inserted as a separate
renderer without duplicating that pass order.

The OpenXR mask differs by eye. In multiview, one draw broadcasts geometry to
both views, so the candidate Vulkan path needs per-view vertex selection and
must handle unequal triangle counts, using degenerate padding if a single draw
is kept. Apply each eye's current asymmetric projection and Vulkan Y convention;
disable culling and depth writes in the mask draw. The backend's flattened
lists avoid any assumption about shared index topology. A renderer snapshot
must refresh even when the pointer and triangle count stay the same, and become
inert on missing/invalid data, session recreation and device loss.

The inherited OpenVR renderer draws a hidden-area depth/color mask after the
clear, and draws black into hidden regions again after screen and UI output.
Matching visible behavior therefore requires final black coverage as well as
scene fragment rejection. The Vulkan postprocess subpass now draws the projected
per-eye mask in black after the ordinary image. SSAO samples neighboring depth, water warp samples
displaced color, palette work samples nearby pixels, and bicubic upscale has a
4×4 source footprint. Masked source pixels near the lens boundary could leak
artifacts into visible pixels. Keep a justified guard region or bypass the mask
for configurations whose sampling radius has not been qualified.

The smallest implementation slice reuses the existing world recording path:
record a fully initialized stencil mask before world chunks and a final black
draw after UI/postprocessing. Initially gate this to native-resolution,
single-sample stereo without SSAO, neighboring screen effects, OIT, or rate
maps. Other configurations retain the current unmasked behavior. Qualify
unequal/asymmetric masks, changed and missing masks, a moving model, both-eye
images, actual fragment rejection and net GPU timing. Then expand and qualify
MSAA coverage, SSAO neighborhood sampling, WBOIT/MBOIT, water/palette/upscale,
and density's coarse-color/full-rate-depth replay. Desktop remains on vkQuake's
original render path. The work is not complete until the applicable VR render
combinations have equivalent visible output and safe mask behavior; the user
will perform physical headset tests later.

## Astra senior review disposition, 2026-09-27

The read-only review used `gpt-6-astra` at `max` effort. Main spot-checked the
load-bearing claims in `Quake/r_passes.c`, `Quake/gl_rmisc.c`,
`Quake/gl_rmain.c`, `Quake/vr_openxr.h`, and the SSAO/screen-effect/upscale
shaders. The reviewer made no edits or builds.

| Review finding | Decision |
| --- | --- |
| The inherited mask includes final black coverage, not only scene rejection. | Adopt both behaviors as acceptance requirements. |
| Postprocessing can sample across a masked boundary. | Adopt a guard-region or mode-bypass policy until each effect is qualified. |
| First-pass stencil is not unconditionally cleared; later owners overwrite it. | Adopt a world-scoped first slice and preserve existing stencil owners. |
| OpenXR vertices require per-eye projection; mesh refresh is not detectable from pointer/count alone. | Adopt per-eye projection and explicit refresh/snapshot ownership. |
| Defer broad integration pending correctness and timing. | Adapt: sequence the required parity work; do not remove it from the migration goal. |
| Add a new frame graph or use the near-depth shortcut. | Reject; both conflict with the existing renderer or depth behavior. |

The precise implementation schedule and measured GPU savings are not yet
established. Backend mesh export alone is not proof of GPU savings.

The per-eye `VRXR_ProjectHiddenAreaVertex` math helper maps backend view-space
vertices through the current asymmetric FOV with Vulkan Y inversion. Its
focused fixture checks unequal eye frusta, corner placement, and invalid input
handling. The final-black Vulkan draw now consumes it, with both eyes packed
into one multiview vertex stream and extra triangles made degenerate. A missing,
oversized, or invalid mesh skips both eyes. `vr_hidden_area` controls this draw
and appears in VR options. The scene still renders into the hidden region;
world-fragment rejection now has a restricted first path; qualification of
neighboring effects remains.

## Scene stencil proof and follow-up review, 2026-09-27

The frame's projected two-eye mesh is now allocated after dynamic-buffer
rotation, before the world-record tasks, and reused by the final-black draw.
In the stereo scene pass, world context zero clears stencil and writes bit 7
over the hidden triangles. Only static opaque world draws test bit 7; direct
and indirect paths select the same two fullbright pipeline choices for the
active standard, WBOIT, or MBOIT main pass. The mask is recorded before the world MVP setup so the
postprocess-layout bind cannot invalidate world push constants. Later sky,
entity, SSAO, and UI stencil behavior is untouched.

The `gpt-6-astra`/`max` read-only follow-up review checked the live code. Main
spot-checked its ordering and pipeline observations against `r_passes.c`,
`gl_rmain.c`, `gl_rmisc.c`, `r_world.c`, and `r_brush.c`. The [Vulkan clear
rules](https://docs.vulkan.org/spec/latest/chapters/clears.html),
[rasterization ordering](https://docs.vulkan.org/spec/latest/chapters/primsrast.html),
and [secondary command execution](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdExecuteCommands.html)
support clear → mask write → stencil reads within the ordered scene subpass.
No new pass or extra stencil barrier is needed for that sequence.

| Review recommendation | Disposition |
| --- | --- |
| Treat the restricted gate as a proof, not production-complete masking. | Adopt. Normal VR still enables SSAO, so it currently uses final black without world-fragment savings. |
| Preserve the world push constants after binding the mask pipeline. | Adopt. Record the mask before `R_SetupContext`. |
| Limit readers to the static opaque world subset in both direct and indirect paths. | Adopt. Both selectors use the same mask scope; moving brushes and cutouts remain unmasked. |
| Expand directly into SSAO without restoring hidden-region depth. | Reject. SSAO samples neighboring and mipmapped depth, so final black cannot preserve visible AO by itself. |
| Reuse a depth-only world replay for SSAO, then qualify MSAA and OIT variants. | Adapt for the next increment. Reuse the existing density-map replay recipe, but measure the added geometry cost and compare visible pixels and depth before enabling defaults. |
| Add a second frame graph, near-depth masking, or global early stencil tests. | Reject. Existing pass and stencil owners already provide the narrow boundary. |

The next increment creates mask-writer and static opaque world-reader pipelines
for all three main-pass variants. The pipeline multisample state follows
vkQuake's selected sample count, allowing MSAA and sample shading instead of
disabling the mask for those modes. OIT transparency still uses its original
pipeline and pass order. SSAO, rate maps, and neighboring screen effects remain
gated. The Linux build establishes integration, not headset-visible parity or
a measured speedup; MSAA boundary coverage and OIT composition still require
visual qualification. Default-mode work must preserve SSAO's neighboring depth
and the density-map replay before the mask can be considered broad.

## Default SSAO depth candidate

The next code slice removes the SSAO gate only where an opaque static-world
depth replay is available. Each world secondary records masked color, then
reuses the existing direct/indirect world traversal with the same static opaque
eligibility filter. Its vertex-only depth pipeline tests bit 7 and writes only
hidden samples before `R_PrepareSSAOWorldDepth` builds the SSAO pyramid. Cutout
world surfaces, moving brushes, and entities remain on vkQuake's ordinary
depth path. This preserves a full depth image for AO sampling without running
the costly world fragment shader over hidden samples. The replay adds vertex
work; on the direct path it also regenerates index batches, so net performance
must be measured before claiming a gain.

The masked opaque color pipeline now uses a separate shader variant with
`layout(early_fragment_tests) in`, and the cutout `discard` code is compiled
out of that variant. The ordinary world shader and its alpha-test behavior are
unchanged. This makes stencil rejection occur before world fragment shading as
specified by the [Khronos GLSL early-fragment-tests rule](https://docs.vulkan.org/glsl/latest/chapters/variables.html).
The generated SPIR-V was validated and contains `EarlyFragmentTests` with no
`OpKill`. No VR GPU time or headset image comparison has yet been collected.

Qualification should compare `vr_hidden_area 0` and `1` at matched resolution,
MSAA, SSAO quality, and OIT mode, including a large static map such as mj4m1.
Check visible AO at the mask edge, both-eye MSAA boundaries, depth-based
effects, and whole-frame GPU/CPU timing. If replay costs more than masking
saves, keep final black coverage and leave scene rejection off for that mode
until a smaller guard or replay region is demonstrated.

The follow-up `gpt-6-astra`/`max` read-only review found no source-level
rendering blocker in the updated design. Main spot-checked the shader selection,
per-sample SSAO depth resolve, frame order, and build wiring. The review did not
run the program or measure GPU time.

| Review recommendation | Disposition |
| --- | --- |
| Force early tests only for masked opaque material. | Adopt. The dedicated variant has `EarlyFragmentTests`; cutouts keep the ordinary shader. |
| Reuse the world traversal for hidden-sample depth replay. | Adopt. Replay runs in the existing world secondary before SSAO preparation. |
| Treat default SSAO as performance-qualified now. | Reject. Direct replay regenerates index batches and both routes repeat geometry work; compare complete frame times before claiming a gain. |
| Limit to indirect rendering or replace replay with a fixed guard immediately. | Defer. Neither policy is justified by measured cost or a conservative AO sampling bound. |
