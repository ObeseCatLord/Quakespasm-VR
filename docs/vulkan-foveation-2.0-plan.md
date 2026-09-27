# Vulkan/OpenXR foveation on the 2.0 branch

The vkQuake renderer remains the graphics owner. Its existing multiview scene,
compiled pass order, secondary command buffers, and internal color/depth targets
remain in place. Eye-tracked foveation is an optional reduction in **opaque
world fragment shading**; it does not cull geometry or change PVS, frustum,
weapon, HUD, water, cutout, or transparent rendering.

## Runtime route priority (September 2026)

Prefer the runtime-managed `XR_FB_foveation` +
`XR_FB_foveation_configuration` + `XR_FB_foveation_vulkan` +
`XR_FB_swapchain_update_state` + `XR_META_foveation_eye_tracked` route **when
the complete extension set, Vulkan density-map feature, and end-to-end image
contract are available and qualified on that runtime/device pair**. Valve
[lists exactly this route for Steam Frame](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom),
so qualify it first for both PC streaming and Linux ARM standalone. The META
profile lets the runtime choose the per-eye gaze pattern without requiring an
application-visible gaze action. Do not use a headset-name allowlist: query the
active runtime and graphics device. Other headsets exposing this complete route
may use it after the same qualification.

The route is not universally more compatible. Khronos specifies
[`XR_META_foveation_eye_tracked`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_META_foveation_eye_tracked.html)
as an optional, unratified extension dependent on both FB extensions; the
[Vulkan foveation swapchain flag](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainCreateFoveationFlagBitsFB.html)
also requires `VK_EXT_fragment_density_map`. The [Khronos runtime inventory](https://github.khronos.org/OpenXR-Inventory/runtime_extension_support.html)
does not list FB/META foveation for Monado's desktop Linux runtime in its
published submissions, and is not a substitute for querying the installed
runtime. Keep `XR_EXT_eye_gaze_interaction` plus vkQuake's existing
`VK_KHR_fragment_shading_rate` backend for Beyond 2e/Monado and any other
runtime with gaze but without the complete FB/META route. If neither usable
path exists, render at full rate. A runtime/device switch currently requires
a renderer restart because the Vulkan feature selection is made at device
creation.

This is a priority for qualification, **not** an unconditional default switch:
the FB/META code remains behind `-vk-runtime-foveation` until borrowed-image
format, layout/readiness, gaze alignment, protected-depth replay, and net GPU
frame time are proven on the target runtime. In particular, the extra
coarse-world/depth-replay passes can erase a density-map shading gain on some
maps. Compare both routes where available at the same resolution and scene,
including `mj4m1`, before preferring one for performance. `vr_eye_tracking`
must still gate eye mode; inaccessible or invalid gaze must restore full-rate
rendering. Fixed foveation remains explicit opt-in and is never a fallback.

## User contract

- `vr_eye_tracking` and `vr_foveation` are archived and now default to `1` and
  `2` respectively on fresh configurations. That requests eye-tracked
  foveation only where a working gaze path is available. `vr_foveation 1`
  explicitly selects fixed foveation. No capability, gaze failure, or missing
  provider selects fixed foveation automatically.
- KHR eye mode requires enabled tracking, a focused rendering frame, a valid
  and tracked `XR_EXT_eye_gaze_interaction` ray with a known fresh expressed-pose
  time, and three consecutive usable frames. The proposed FB/META path uses
  its own per-frame validity signal and the same three-frame stability rule;
  it need not expose a separate application gaze action. Any failure returns
  full-quality shading immediately.
- The gaze action uses `XR_EXT_eye_gaze_interaction` and may be supplied by any
  conforming runtime/provider. Beyond 2e with Monado, Steam Frame streaming,
  Steam Frame standalone, Windows, and Linux ARM still need physical/runtime
  qualification. Eye tracking is not set up on the user's Beyond yet.
- Valve's [Steam Frame custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
  explicitly recommends OpenXR and lists `XR_EXT_eye_gaze_interaction` plus
  the FB/META runtime foveation extensions. Its [standalone deployment guide](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames)
  supports a Linux ARM64 binary under Steam Linux Runtime 3.0 ARM64 (Sniper);
  Android 10 is an alternative target, not a requirement for this fork.
  Streaming from a PC and running the Linux ARM64 binary on the headset remain
  separate release checks. Valve's [Frame input guide](https://partner.steamgames.com/doc/steamhardware/steamframe/input)
  documents the native OpenXR controller profile and Touch fallback. The
  imported backend suggests both profiles; profile presence alone is not a
  substitute for testing button and pose behavior on hardware.
- Desktop Vulkan and headsets without accessible gaze retain their existing
  rendering. Fixed mode is optional even without eye tracking.

## Minimal renderer adapter

1. Negotiate `VK_KHR_fragment_shading_rate` and RenderPass2 only for a capable
   OpenXR Vulkan device. Query attachment limits and the rates supported for
   the **actual scene MSAA sample count**. Do not enable a density-map feature
   in the same device configuration.
2. Add one renderer-owned `R8_UINT` shading-rate image to the scene framebuffers.
   Use two layers when supported; otherwise use one layer whose tiles select
   the finer of the two eyes' requirements. Derive dimensions from the scene
   render extent, not the XR swapchain extent. Keep the image single-sampled.
3. Extend the existing pass compiler's creation boundary to emit RenderPass2
   scene passes with `VkFragmentShadingRateAttachmentInfoKHR`. Preserve its
   pass topology, variants, attachment policy, and secondary inheritance.
   Desktop and UI creation retain their current path. The rate attachment
   cannot be cleared by a render-pass load operation.
4. Upload the entire rate map through the existing staging/graphics queue.
   Synchronize previous shading-rate reads before transfer writes, and transfer
   writes before subsequent shading-rate reads, for all map layers. Its GPU
   lifetime is scene-resource lifetime; staging allocations keep their existing
   command-buffer-slot ownership. The scene framebuffer index is selected by
   postprocessing and is **not** the command-buffer fence slot.
5. Select `KEEP/REPLACE` only on eligible opaque, non-cutout static BSP world
   draws. Use `KEEP/KEEP` on ineligible draws sharing world pipelines. Emit the
   dynamic state in each secondary command buffer after binding, including
   direct batches and indirect draws. Other pipeline families retain Vulkan's
   static 1x1 `KEEP/KEEP` default. Supersampling continues to request sample
   shading, which forces effective 1x1; the rate-map path is disabled there.
6. Preserve existing VR culling: eye-union PVS, per-eye visibility, and
   conservative backface tests. Foveation never removes peripheral geometry.

The old `openxr` branch provides reusable gaze freshness, eye-ray and angular
tile math, plus a prior Vulkan implementation to consult. Its separate renderer
is not imported. Runtime FB/META fragment-density-map foveation now has an
explicit `-vk-runtime-foveation` development path: it requests borrowed density
images, creates runtime profiles, updates the selected profile each frame, and
uses a separate coarse-world pass with full-rate depth replay. This path is
**not** automatically selected. The borrowed-image format, layout/readiness,
gaze alignment, and protected-depth behavior still need target-runtime proof.
The KHR path remains available on capable devices without a qualified runtime
route; the two Vulkan feature paths require a device restart to switch.

Valve's [Steam Frame custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
lists the FB/META extensions for Frame. The [Khronos FB Vulkan extension](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_FB_foveation_vulkan.html)
defines the borrowed density image, and the [META eye extension](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_META_foveation_eye_tracked.html)
adds an eye-tracked profile. These are runtime capabilities, not headset-wide
guarantees. Prefer FB/META only on runtime/device pairs that pass the
end-to-end proof, including Steam Frame streaming and standalone; keep KHR or
full-rate rendering elsewhere. The user eye-tracking toggle gates eye mode,
and fixed foveation remains explicit opt-in only. See the
[Steam Frame senior design disposition](migration-steam-frame-foveation-review.md)
for the pass, image, and fallback constraints. That review requires a separate
coarse-world pass followed by full-rate world-depth replay before protected
draws; a depth prepass is not a portable substitute under FDM.

## Astra senior-review disposition

| Finding | Decision |
| --- | --- |
| Framebuffer index does not identify a fence-protected rate image | Adopt one scene-owned image with explicit barriers first; add per-slot images only if profiling justifies them. |
| World pipeline identity also includes bmodels, decals, water and cutouts | Adopt draw-level eligibility and dynamic state; avoid duplicate static coarse pipelines. |
| A parallel renderer or pass compiler would duplicate vkQuake policy | Reject; adapt the existing pass compiler at RenderPass2 emission. |
| MSAA, supersampling and non-layered multiview affect savings/correctness | Adopt sample-aware rates, report no benefit under supersampling, merge eye maps to the finer rate when single-layer. |
| FB/META profile update is separate from KHR shading-rate images | Keep it out of the KHR path; qualify a runtime-density-map backend later. |

## Implementation and qualification status

The opt-in KHR path, VR options, gaze policy, rate-map math, scene-pass adapter,
world-draw eligibility, image lifecycle, and staging barriers are implemented
on `2.0`. A forced full Linux debug build and the focused policy/map fixtures
pass. An isolated simulated-Monado run with `vr_foveation 1` completed all 24
existing OpenXR scene probes across direct/indirect drawing, 1x/4x MSAA, all
OIT modes, resize, pause, and teardown. A separate live breakpoint confirmed a
two-layer 56x63 map at 4x MSAA with full-rate center and coarser outer tiles.
Khronos standard plus synchronization validation passed the same 24-probe run
without a VUID or synchronization hazard after the image-allocation `sType`
fix. The test used a temporary validation-layer extraction and private Monado
runtime/config directories; it did not change the installed runtime or game.

The next qualification must render a real multiview scene with injected gaze,
compare both eyes with foveation off, move and invalidate gaze, and check that
weapon/UI/water/cutout/transparent content remains full quality. The focused
fixture covers gaze movement and one-layer finer-eye mapping, but there is no
end-to-end gaze-provider or forced nonlayered-GPU proof yet. Profile CPU/GPU
frame times and visual quality on `mj4m1` and other large maps before claiming
a performance gain; the shared-image rewrite barrier may serialize frames.
For Steam Frame standalone, use Valve's [Performance Assessment Overlay](https://partner.steamgames.com/doc/steamhardware/steamframe/compat/perf_criteria)
to capture effective resolution, frame rate, target frame time, and transient
violations during normal gameplay. The [standalone review criteria](https://partner.steamgames.com/doc/steamhardware/steamframe/compat)
require at least 72 fps at 1728×1728 for VR. Compare foveation off, qualified
eye tracking, and explicit fixed mode at the same render scale and scene; report
GPU frame time and both-eye visual quality, not only the runtime's chosen profile.
Physical Beyond/Steam Frame eye tracking, Windows, and Linux ARM verification
remain deferred as requested.

Astra Max's final read-only code review through `45de8d0a` found no remaining
high- or medium-severity defect. It independently checked map synchronization
and retirement, protected draw paths, default-off and gaze-loss behavior, and
the multiview one-layer rule. Real gaze alignment/reacquisition, single-layer
hardware execution, and measured net performance remain qualification gates,
not demonstrated failures.

Official references: [OpenXR gaze system support](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSystemEyeGazeInteractionPropertiesEXT.html),
[KHR shading-rate attachment and encoding](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-fragment-shading-rate-attachment),
[RenderPass2 attachment](https://docs.vulkan.org/refpages/latest/refpages/source/VkFragmentShadingRateAttachmentInfoKHR.html),
[device limits](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFragmentShadingRatePropertiesKHR.html),
[multiview framebuffer layer rule](https://docs.vulkan.org/refpages/latest/refpages/source/VkFramebufferCreateInfo.html),
[attachment load/store semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription2.html).
