# Steam Frame runtime foveation: senior design disposition

Reviewed on 2026-09-26 against branch `2.0` by Astra (read-only). This is an
implementation decision and proof plan, not a claim of Steam Frame support or
measured performance. Valve's [custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
lists the FB/META runtime foveation extensions and `XR_EXT_eye_gaze_interaction`.
Its [Unreal guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/unreal)
recommends runtime-provided VRS. Neither document establishes the headset's
Vulkan feature bits in this application.

## Decision

Keep vkQuake's scene targets, pass compiler, postprocess, multiview and XR
borrowed-image owner. Add a *conditional* runtime fragment-density-map (FDM)
adapter only after the protected-content proof below. The current portable
`VK_KHR_fragment_shading_rate` backend remains operational meanwhile. OpenXR
extension discovery alone must not switch backends or request density maps.
Once the runtime path qualifies end to end, prefer it on **any** runtime that
offers the required XR extensions, eye-foveation system property and Vulkan FDM
device/image features; use KHR on devices where that runtime route is not
qualified. This is capability selection, not a Steam Frame or headset-name
allowlist. Valve [documents the FB/META route for Steam Frame](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom),
and Meta [documents the FB Vulkan route for native OpenXR](https://developers.meta.com/horizon/documentation/native/android/os-fixed-foveated-rendering/);
neither document proves support on all runtimes or headsets.

| Review finding | Disposition |
| --- | --- |
| Runtime FDM is pass-wide; the current scene pass mixes opaque world with alpha-tested surfaces, weapons, particles and transparency. | **Adopt.** Isolate eligible opaque rendering in a graphics pass. Do not assume another subpass or KHR draw-level rate state protects those draws. |
| Coarse rendering can leave depth coverage unsuitable for full-rate protected draws. | **Adopt.** Prove or rebuild authoritative full-rate depth before protected content; thin occluders and both eyes are required visual checks. Pass splitting alone is insufficient. |
| FDM and KHR fragment-shading-rate device features are mutually exclusive ([VUIDs 04481–04483](https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html)). | **Adopt.** Choose one feature family before device creation. After FDM-device commitment, an FDM failure falls back to full rate; using KHR requires device recreation. Never silently select fixed foveation. |
| FDM may require subsampled attachments when `fragmentDensityMapNonSubsampledImages` is absent. | **Adopt conditional path.** Prefer non-subsampled scene targets when supported. Otherwise, a bounded subsampled scene/reconstruction bridge must satisfy load, sampler, input-attachment and storage rules before activation; do not blanket-flag existing targets. [Khronos sample](https://docs.vulkan.org/samples/latest/samples/extensions/fragment_density_map/README.html). |
| Runtime maps belong to acquired XR swapchain image indices, while current scene framebuffers follow internal color-buffer indices. | **Adopt.** Make that mapping explicit and retain borrowed image lifetime under XR ownership. Initially require matching scene and XR extents as a conservative proof constraint, not a claimed OpenXR rule. [OpenXR image contract](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainImageFoveationVulkanFB.html). |
| Optional setup failure currently leaves VR unattached if density maps are requested. | **Adopt.** Retire partial XR resources and retry once without density maps while the runtime is healthy; preserve terminal-loss handling. |
| Replacing the renderer or drawing directly to XR would duplicate existing scene, composition and resource policy. | **Reject.** Adapt the existing compiler and image boundaries. Reopen the architecture decision if the bridge grows into a second render graph. |

## Smallest end-to-end proof

1. Query `VK_EXT_fragment_density_map`, its feature bits and image rules before
   Vulkan device creation. The [OpenXR foveation flag](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainCreateFoveationFlagBitsFB.html)
   requires that Vulkan extension to be enabled. Keep KHR selected until the
   complete FDM path qualifies; do not add a dormant device switch that removes
   the working KHR backend. `gl_vidsdl.c` now reports a query-only FDM candidate
   when the Vulkan feature, RG8 array-image limits and RenderPass2 are present;
   it does not enable the feature or request a density swapchain.
2. Use one stereo array swapchain, matching scene/XR extent and single-sample
   opaque world. Attach the acquired runtime map to a scene graphics pass using
   the existing compiler, and establish its format, layout, read point, layer
   count, framebuffer identity, synchronization and retirement under validation.
3. Continue with full-rate cutouts, weapon, transparency, existing effects and
   postprocessing/UI using proven full-rate depth. Compare both eyes through
   profile off/on/off, moving gaze, lost gaze and thin occluders. Reuse the
   existing gaze freshness/stability policy and profile function; invalid gaze
   selects off/full rate immediately. The [META profile structure](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFoveationEyeTrackedProfileCreateInfoMETA.html)
   has `flags=0`, which the imported backend already uses. The older
   `openxr` renderer manually applied QCOM density-map offsets from the
   returned center. Do not copy that step without proving it is needed:
   [Khronos describes `xrUpdateSwapchainFB` immediately before the META state
   query](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetFoveationEyeTrackedStateMETA.html)
   as a request for the runtime to update the foveation pattern. The returned
   center is observation, not proof of an application offset requirement.
4. Preserve all established MSAA, OIT, render-scale and desktop settings. If a
   combination cannot satisfy the FDM image/depth contract, render that
   combination full rate. Measure GPU frame time and visual quality on `mj4m1`
   before claiming speedup; use Valve's [Frame performance overlay](https://partner.steamgames.com/doc/steamhardware/steamframe/compat/perf_criteria)
   for standalone qualification.

Stop if borrowed-map layout/readiness or offset semantics remain unclear,
attachment reads become undefined, protected content loses depth/coverage, or
the implementation duplicates vkQuake pass policy. Hardware capability and
eye-provider testing remain user-side qualification, not assumptions in code.

If Frame lacks `fragmentDensityMapNonSubsampledImages`, evaluate the smallest
subsampled reconstruction bridge against the user's runtime-first preference
and the protected-depth proof. Reopen the architecture decision if that bridge
duplicates vkQuake's pass policy or loses a measured performance benefit.
Until qualification succeeds, keep the existing KHR path where available and
full-rate rendering otherwise, with no automatic fixed-foveation fallback.
