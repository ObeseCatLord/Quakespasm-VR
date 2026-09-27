# OpenXR hidden-area mask boundary

`XR_KHR_visibility_mask` can avoid rasterizing pixels outside the headset's
visible lens area. The OpenXR backend already negotiates the optional extension,
refreshes its per-eye mask on the runtime change event, and exports finite,
index-checked, flattened hidden triangles through `VRXR_GetHiddenAreaMesh`.
The vkQuake Vulkan frame recorder does not consume that mesh yet. Missing masks
must leave full scene quality; no headset-specific mask is assumed. This remains
an inherited VR behavior to migrate, even if the first implementation must be
limited to configurations whose effects are qualified. The [official OpenXR
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
established. The current backend mesh export is source readiness, not a working
Vulkan hidden-area mask.

The per-eye `VRXR_ProjectHiddenAreaVertex` math helper maps backend view-space
vertices through the current asymmetric FOV with Vulkan Y inversion. Its
focused fixture checks unequal eye frusta, corner placement, and invalid input
handling. The final-black Vulkan draw now consumes it, with both eyes packed
into one multiview vertex stream and extra triangles made degenerate. A missing,
oversized, or invalid mesh skips both eyes. `vr_hidden_area` controls this draw
and appears in VR options. The scene still renders into the hidden region;
world-fragment rejection and qualification of neighboring effects remain.
