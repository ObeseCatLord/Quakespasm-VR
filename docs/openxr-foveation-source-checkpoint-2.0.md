# Current two-view foveation source checkpoint

2026-09-30. Source reconciliation, not another rendering implementation or an
executable acceptance result. The accepted defaults decision in
[openxr-foveation-defaults-2.0-plan.md](openxr-foveation-defaults-2.0-plan.md)
supersedes historical development-gate and borrowed-image release-blocker
labels. Quad views remain excluded. Main inspected current producers and
consumers; Luna xhigh updates only the corresponding inventory cells.

| Requirement | Current producer and consumer | Scope and remaining proof |
| --- | --- | --- |
| XR-005 KHR fallback | `gl_vidsdl.c:GL_FoveationRequestedActive` checks the selected Vulkan device, supported rates/native samples, stereo, dimensions and current settings. `GL_PrepareFragmentShadingRateMap` uses `VRF_SelectMode` and the existing layered or shared two-eye rate-map builder; `GL_UploadFragmentShadingRateMap` stages only changed maps with the existing queue/barriers. | Desktop preference and startup fallback are actual selection policy. A device created for FDM cannot switch live to KHR. Final device/sample/rate-map/upload qualification remains. |
| Protected geometry | `glquake.h:R_WorldFoveationEligible` admits only opaque ordinary world surfaces. `r_world.c:R_FlushBatch` and `r_brush.c:R_DrawIndirectBrushesFiltered` consume that decision. `gl_rmisc.c:R_CreateWorldPipelines` adds dynamic shading rate only to eligible opaque pipeline variants; `R_SetWorldFragmentShadingRate` keeps1x1 for protected draws. | UI, weapon/entity models, cutouts, blended/liquid/sky/decal content retain native full-rate paths. Final direct/indirect and material-boundary images remain required. |
| XR-006 FB/META preference | `gl_vidsdl.c` device selection uses complete runtime/GPU/sample/offset candidates, explicit fixed requests or its no-KHR preparation alternative, with mutually exclusive FDM/KHR features. Settings readiness excludes sample shading and explicit render-size overrides. `GL_DensityFoveationRequestedActive` checks actual paired views, supported attachment formats and the separate fixed/eye profile capabilities. | No development CLI gate remains. Ordinary native MSAA is retained; these guards do not certify borrowed allocation metadata. |
| Runtime frame state | `VRXR_UpdateVulkanFoveation` requires the begun rendering frame and acquired/waited, unsubmitted images. It selects fixed only for mode1, eye only for mode2 with focus/toggle/capability, updates the profile, checks both META centers and validity, applies stability, and publishes both centers together. `GL_PrepareRuntimeFoveation` runs before render tasks and validates quantized offsets. | Invalid state/centers restore off. Failure to restore off aborts the frame and restarts ordinary density-disabled passes. There is no fixed fallback. |
| Profile lifetime and update ordering | `vr_openxr.cpp:update_foveation_profile` caches successful off/fixed state per existing swapchain, invalidates uncertain state after failed setters, and always updates either eye profile before its query. Normal Chain reset clears the cache. | Repeated eye updates are not accidentally skipped by the static-profile optimization. Setter call counts and partial-chain failure/recreation remain final software cases. |
| Native fine depth and graphics | `gl_rmain.c:R_DrawWorldTask` sends eligible world color to the density scene only while effective coarse foveation is active, replays its full-rate depth, then draws protected content. Lost gaze draws the world once in the ordinary scene. `r_passes.c:R_CreateScenePasses` loads stored coarse MSAA color before the protected resolve. Density pass end submits two eye offsets through the existing Vulkan end owner. | Fine depth remains the input to ordinary protected depth tests and existing AO/OIT owners. Actual two-layer MSAA/AO/OIT/occlusion qualification is pending; source order is not rendered proof. |
| XR-007 explicit fixed only | `view.c` registers eye tracking1 and eye foveation2. `menu.c` selects the shared exact off/fixed/eye modes. `vr_foveation_policy.h:VRF_RequestedMode` rejects malformed modes; `VRF_SelectMode` and the FB/META owner choose off for disabled/unusable eye mode, without selecting fixed. | The eye toggle remains optional and authoritative. Existing fixed profile creation does not activate it. Final default/opt-out/focus/unsupported/mode-transition cases remain. |

Visibility continues to use complete two-eye geometry, conservative frusta and
either-eye PVS; gaze changes shading density, not world visibility. KHR gaze
uses the existing tracked/expressed-time freshness and stability checks. META
returns its current pattern validity/centers through its own runtime interface;
it does not expose the EXT gaze sample timestamp, so the application does not
pretend to apply an EXT timestamp check to that struct.

Re-read [Valve's official custom-engine documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
for this checkpoint: it recommends the FB/META extension family, EXT gaze and
Linux ARM64 as a standalone target. The [official META extension](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/meta/meta_foveation_eye_tracked.adoc)
describes current per-eye centers/validity and recommends updating the swapchain
immediately before querying the pattern. Current code preserves that order.
Neither source establishes performance gains or compatibility for every GPU,
Steam Frame mode or Beyond/Monado provider.

The earlier accepted interoperability convention remains explicit: paired RG8
array layers, auxiliary offset flags and usable incoming layout/readiness are
runtime integration assumptions. Successful view/format creation is not
metadata attestation or GPU producer-completion proof. No additional guessed
barrier, stall, map producer or runtime-name framework is added.

No build, test, compiler probe, fixture, benchmark or game run occurred in this
checkpoint. End-of-implementation Linux x86-64 and isolated native Foundry ARM
qualification remains mandatory, using the existing final matrix. Windows and
user live headset/gaze/multiplayer/performance checks stay deferred. This
reconciles XR-005/007's stale implementation labels; it does not complete the
full migration or certify all XR lifecycle behavior.
