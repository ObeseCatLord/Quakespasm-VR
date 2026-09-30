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
| XR composition currently submits the runtime located FoVs. | [verified: source] `vr_openxr.cpp:submit_frame` copies `g.views[eye].fov` directly. |
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
