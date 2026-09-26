# Vulkan/OpenXR foveation on the 2.0 branch

The vkQuake renderer remains the graphics owner. Its existing multiview scene,
compiled pass order, secondary command buffers, and internal color/depth targets
remain in place. Eye-tracked foveation is an optional reduction in **opaque
world fragment shading**; it does not cull geometry or change PVS, frustum,
weapon, HUD, water, cutout, or transparent rendering.

## User contract

- `vr_eye_tracking` and `vr_foveation` are archived and now default to `1` and
  `2` respectively on fresh configurations. That requests eye-tracked
  foveation only where a working gaze path is available. `vr_foveation 1`
  explicitly selects fixed foveation. No capability, gaze failure, or missing
  provider selects fixed foveation automatically.
- Eye mode requires enabled tracking, a focused rendering frame, a valid and
  tracked gaze with a known fresh expressed-pose time, and three consecutive
  usable frames. Any failure returns full-quality shading immediately.
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
is not imported. Runtime FB/META fragment-density-map foveation is a later,
separate device/pass design; its borrowed-image and protected-depth contract
is not established by the KHR path.

For the Steam Frame standalone target, that later runtime path is a release
feature gap: `GL_OpenXRAttach` currently passes `density_maps=0` to the reused
backend, and `VRXR_UpdateVulkanFoveation` has no renderer callsite. Valve's
[custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
lists those runtime extensions, while its [Unreal guidance](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/unreal)
recommends runtime-provided VRS. Adding it requires an attachment/image-lifetime
adapter inside vkQuake's existing pass graph; the portable KHR path remains
useful on GPUs that expose it. No current source evidence proves either path
works on Steam Frame hardware yet.

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
