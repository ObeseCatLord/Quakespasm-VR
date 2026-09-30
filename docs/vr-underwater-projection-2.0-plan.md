# VR alternate underwater warp: design brief

Date: 2026-09-29. Branch: `2.0`. Solo-maintainer scope; use the existing
vkQuake renderer and two-view stereo. Quad views are excluded.

## Goal and verified evidence

Preserve native desktop `r_waterwarp` behavior and provide its alternate
FOV oscillation in VR rather than silently discarding it. This is the opt-in
mode other than `0`/`1`; the already layered mode `1` screen warp remains.

| Claim | Evidence/status |
| --- | --- |
| Native alternate warp changes tangent width by `0.97 + sin(time*1.5)*0.03` and height by `1.03 - sin(time*1.5)*0.03`. | [verified: source] `gl_rmain.c:R_SetupViewBeforeMark`; readonly vkquake `4bc898f29073e8aa41069f0e79e3cb5a9eb73afa` uses these FOVs in projection. |
| Current VR ignores those FOVs. | [verified: source] `R_SetupMatrices` keeps a symmetric 90-degree center projection; actual per-eye clips come from `gl_vidsdl.c:GL_BeginRendering` calling `VRXR_StereoClip` before the world contents query. `render_warp` is false for mode 2, so no screen replacement occurs. |
| Eye tangents have multiple consumers. | [verified: source] `R_PrepareStereoFrame` builds conservative visibility from runtime rays; `r_ssao.c:R_SSAOSetEyeProjection` reconstructs depth from runtime tangents; hidden-area math and gaze/foveation preparation also consume the ordinary XR frame. |
| XR composition currently submits the runtime located FoVs. | [verified: source] `vr_openxr.cpp:end_frame` copies `g.views[eye].fov` directly. |
| Ordinary layered effects and resources work through the native pass graph. | [verified: bounded Astra source audit] `r_passes.c` screen effects and `r_ssao.c` stereo adaptation; this is not a pipeline/provider or headset certification. |
| Distorted projection versus compositor FoV, hidden mask and runtime density-map alignment. | [unknown: design contract] Must settle before implementation. No runtime experiment is authorized in this phase. |

Environment: Linux and Linux ARM targets; desktop graphics remain native;
Windows builds deferred. Source owners are `gl_rmain.c`, `gl_vidsdl.c`,
`vr_openxr_math.h`, `r_ssao.c`, screen-effects constants/shader and the existing
XR frame. Eye tracking is optional; fixed foveation is explicit only. Do not
add another pass graph, XR swapchain owner or general Vulkan recovery machinery.

## Options and current lean

1. **Projection adapter (initial lean, unapproved).** Scale each eye's tangent
   coordinates using the native factors. Rebuild actual clip/culling and AO
   reconstruction from the same effective view at the existing frame boundary.
   Keep runtime view metadata separately; investigate whether the composition
   layer must retain the undistorted FoV for intentional visual distortion.
   Hidden masks and eye foveation must refer to the final displayed pixels,
   not be casually transformed along with scene rays. No new draw pass.
2. **Existing screen-effects adapter.** Carry native oscillation into its
   layered image transform, after AO, while retaining projection and XR metadata.
   Zoom-out needs a correctly rendered overscan region or an explicit bounded
   visual compromise; clamped edge samples alone do not reproduce native
   peripheral visibility. Assess complexity before preferring this route.
3. **Use mode 1 for all VR water warps.** Simplest, but changes the selected
   effect and does not meet the native graphics intention; rejected unless
   evidence makes the requested visual effect unsuitable and the tradeoff is
   explicitly documented. Silently ignoring mode 2 is rejected.

These decisions overlap: projection, AO, culling, masks and foveation are one
coordinate contract. Review them together rather than adding independent
policy/state owners. The initial lean is not permission to change Vulkan.

## Review and bounded implementation

Local Astra should first verify these load-bearing source claims, then rank
the options, identify the minimum coordinate contract and deletion opportunities,
and return a prioritized disposition recommendation (about 800 words). Consult
official XR/Vulkan documentation where source alone cannot settle composition
or foveation semantics. No nested delegation, builds, tests or hardware probes.
Do not review avatars, netcode, device reconstruction, quad views or skyrooms.

Before production code, record the main agent's spot-check and disposition,
commit the chosen coordinate contract, file ownership and expected patch size.
If the solution requires new render targets/overscan or substantial pass-graph
work, reopen the decision rather than implement a broad renderer rewrite.

End-of-implementation software qualification should cover underwater/air
transitions, `r_waterwarp 0/1/2`, asymmetric/canted eye views, frustum-edge
visibility, matching AO reconstruction, protected UI, foveation on/off and
worldless frames. Live binocular appearance and comfort remain user checks;
no performance or visual qualification is claimed from source review alone.

## Local Astra advisory design disposition

One existing local reviewer was requested as `gpt-6-astra`/`max`; its response
reports that effective model/effort metadata was unavailable. This is a bounded
advisory review, not a certification of effective Max settings or executed
software acceptance. Main-agent spot-checks confirmed the preparation order,
native factors, existing asymmetric projection, AO consumers, and the backend's
unchanged submitted FoV.

| Recommendation | Main disposition and verified reason |
| --- | --- |
| Scene-only projection deformation; unchanged composition/display FoV. | Adopt. The official [projection-view contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerProjectionView.html) says the runtime maps submitted views/FoVs to the display. Keeping the ordinary display FoV while intentionally deforming scene rays is the chosen effect; compositor compensation for a changed FoV would defeat it (inference from that contract). |
| Scale both tangent endpoints about the eye optical axis, using one phase. | Adopt. Multiplying both endpoints preserves the asymmetric projection offset terms; replacing only the tangent span around its midpoint would introduce translation. Keep the fixed 90-degree intermediate and the existing relative eye transform. |
| Settle publication order before touching clip values. | Adapt. Select the factors in `R_PrepareStereoFrame` after tracked center placement, using that center's leaf contents. This existing setup task precedes `before_mark`, drawing and GUI. Its contents query mirrors the subsequent native view query; no new task or state owner. Publish the effective clip before consumers record commands. |
| Keep masks and foveation in display coordinates. | Adopt. Official [visibility-mask documentation](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrVisibilityMaskKHR.html) defines coordinates for the composition projection; the existing hidden mesh already uses raw runtime tangents. [META foveation centers](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFoveationEyeTrackedStateMETA.html) are NDC positions. Leave `openxr_frame.views`, gaze-to-pixel mapping, hidden-mask vertices and runtime density-map centers unchanged. No additional foveation backend or fallback. |
| Preserve unwarped protected UI. | Adopt with the existing subpass boundary. Main verified `basic.vert` and panel MVPs use the stereo uniform; changing the only allocation would warp panels. `R_BindPipeline` already knows `cbx->subpass_type`, so the existing `SUBPASS_UI` can select the ordinary display allocation while scene passes use an effective one. No new shaders/passes/targets. |
| Screen scaling with clamped edges or mode-1 substitution. | Reject. Neither supplies the native alternate effect's additional peripheral geometry. New overscan targets are unnecessary with the projection adapter. |

### Selected implementation contract, committed before code

Expected scope: `gl_rmain.c`, `glquake.h`, `gl_vidsdl.c`, `r_ssao.c` and
`vr_openxr_math.h`, roughly 100–180 lines. No XR backend or pipeline-layout
changes. Retain the existing raw clip allocation and allocate a second immutable
scene uniform only when the factors differ from identity; otherwise alias the
same allocation. Select by existing `SUBPASS_UI`, including flat GUI and world
panels. This is display/scene data for the same frame, not two frame owners.

Store only the frame's two scalar factors at the existing stereo preparation
owner. A small accessor derives an effective eye view from raw data for both
clip math and AO; culling applies the identical factors to its eye rays. Do not
copy the XR frame or alter pose, near plane, compositor FoV, runtime gaze or
density maps. Reset factors and both descriptor publications each frame,
including worldless/air/modes 0 and 1. A failed effective projection returns to
identity/raw clips before any culling or uniform publication. Preserve native
desktop FOV calculations exactly.

Use the existing task edges: begin-rendering → setup-frame → before-mark and
scene/GUI consumers. Allocations occur in setup-frame after frame-slot safety;
the before-mark → GUI edge already exists. Avoid late mutation of mapped uniform
memory and binding selection by mutable global "currently drawing UI" flags.
Source review must specifically trace both descriptor resets and subpass selection;
software and live acceptance remain deferred as above. Reopen the decision if
the patch needs shaders, another pass graph or render targets.

### Source implementation checkpoint

The adapter is implemented within the five planned files; bounded source
acceptance is pending. `R_PrepareStereoFrame` selects native factors and publishes
immutable display/scene uniforms. The identity path shares one allocation.
`R_StereoSceneView` feeds conservative frustum construction and per-eye AO;
`VRXR_StereoClipForViews` reuses the existing projection/relative transform math.
`R_BindPipeline` selects display clips for `SUBPASS_UI` and scene clips otherwise.
Both XR image retirement and fresh frame acquisition clear the scene descriptor.
The backend, frame graph, shader ABI, masks and foveation policy are unchanged.

Only source comparison/reads and `git diff --check` have occurred. No builds,
fixtures, runtime, headset or performance checks were performed.
