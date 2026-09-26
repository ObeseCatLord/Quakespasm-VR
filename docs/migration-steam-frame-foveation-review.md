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
| Coarse rendering can leave depth coverage unsuitable for full-rate protected draws. | **Adopt.** Replay eligible opaque world geometry into cleared, authoritative full-rate depth after the FDM pass and before protected content; thin occluders and both eyes are required visual checks. Pass splitting alone is insufficient. |
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
   profile off/on/off, moving gaze, lost gaze and thin occluders. Use META's
   per-frame validity signal with the shared three-frame stability helper;
   invalid state selects off/full rate immediately. The [META profile structure](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFoveationEyeTrackedProfileCreateInfoMETA.html)
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

## Follow-up Astra senior review: depth and borrowed-image contract

The 2026-09-26 follow-up reviewed the narrower pass/depth design against
`Quake/r_passes.c`, `Quake/r_world.c`, `Quake/r_brush.c`, `Quake/gl_rmisc.c`, and
the earlier `openxr` branch's renderer. The main thread spot-checked its
load-bearing claims. The [Vulkan fragment-operations specification](https://docs.vulkan.org/spec/latest/chapters/fragops.html)
says that depth-sample association is implementation-dependent when a density
map fragment covers multiple pixels. That makes a full-rate depth *prepass*
followed by depth-tested FDM color an unreliable protection scheme.

| Follow-up finding | Disposition |
| --- | --- |
| An FDM pass cannot share the protected-content draw grouping. | **Adopt.** Add one physical scene pass at the existing frame compiler boundary, then continue the existing scene pass full rate. Do not add a second render graph. |
| The prepass depth proposal does not give portable coverage under FDM. | **Replace.** Render eligible opaque world color/depth with FDM, then clear authoritative full-rate depth and replay the same eligible geometry depth-only, followed by cutouts, bmodels, entities, weapons, and transparent work. Replay all world chunks before any protected color, including in the threaded path. |
| The old `openxr` branch already has `VKR_BeginDensityWorld`/`VKR_ReplayDensityWorldDepth` and opaque-bucket replay. | **Reuse the pattern, adapt the implementation.** vkQuake uses reversed depth (`VK_COMPARE_OP_GREATER_OR_EQUAL`, clear zero), so the old renderer's `LESS_OR_EQUAL` state must not be copied. |
| The direct world texture chains and indirect brush command stream mix eligible and protected draws. | **Adopt.** Route by actual world/texture/instance eligibility, not by the current KHR per-draw shading-rate hint alone. Exclude cutouts, decals, all bmodels, liquid, sky, blends, and dynamic objects from FDM. Both rendering modes must replay exactly the geometry used for coarse color. |
| Scene framebuffer indices and acquired OpenXR swapchain indices differ. | **Adopt.** Create/map compatible framebuffers for the cross-product of internal scene slot and acquired runtime map, with XR retaining borrowed-image ownership and retirement. |
| Existing RenderPass2 dispatch is loaded only for KHR shading rate. | **Adopt.** Load it independently for FDM once that backend is selectable. |
| Some devices require subsampled scene attachments. | **Defer.** First support only `fragmentDensityMapNonSubsampledImages` with matched scene/XR extents and one sample. If unavailable, retain the existing KHR route or full rate while evaluating a bounded bridge. |
| The FB Vulkan image enumeration exposes image/width/height, but not explicit format, layout, or read readiness. | **Qualify, do not infer.** `VK_FORMAT_R8G8_UNORM` support plus extension enumeration is insufficient to activate FDM. Confirm the runtime image contract and synchronization against validation and runtime implementation/source before selecting it. |

The [FB Vulkan extension text](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/fb/fb_foveation_vulkan.adoc)
defines the borrowed `VkImage` and its dimensions but does not spell out a
format, initial layout, or host-to-GPU readiness guarantee. As a practical
precedent, [Godot imports the runtime image as an RG8 array texture](https://github.com/godotengine/godot/blob/master/modules/openxr/extensions/platform/openxr_vulkan_extension.cpp)
and [uses META gaze state with QCOM density offsets when available](https://github.com/godotengine/godot/blob/master/modules/openxr/extensions/openxr_fb_foveation_extension.cpp).
Those are implementation evidence, not normative guarantees for Monado, SteamVR,
or every headset. The earlier instruction above to avoid blindly copying QCOM
offsets still stands, but the offset path now has a concrete precedent and must
be explicitly qualified rather than dismissed. The [Vulkan render-pass rules](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#VkRenderPassFragmentDensityMapCreateInfoEXT)
allow the default map view to be read by the host when `vkCmdBeginRenderPass`
is recorded; deferred/dynamic views have different read points. A GPU barrier
alone does not establish host-read readiness. Per-eye offsets also require the
offset feature and creation flags on every used framebuffer attachment, plus
granularity and layer-count compliance. Qualify these before adding offsets;
if the required path cannot be established, select KHR or full rate.

The user's compatibility preference is runtime-first **after qualification**:
use XR_FB/XR_META on any runtime/device pair that completes this proof, keep
the working KHR shading-rate path on other capable devices, and use full rate
otherwise. Eye tracking remains optional; loss or invalid gaze gives full rate,
and fixed foveation is only enabled by an explicit user choice. Runtime FDM is
not active in the current renderer because `GL_OpenXRAttach` still requests
`density_maps=0`.

`R_DrawWorldFiltered` and `R_DrawIndirectBrushesFiltered` now expose eligible,
protected and unchanged all-draw selections through the existing world draw
code. The current frame compiler still calls the all-draw wrappers, so this
does not split or activate an FDM scene pass. The next integration must bind
these selections to globally ordered coarse-color, depth-replay and protected
stages. For large maps, measure the cost of iterating indirect draws multiple
times and optimize that routing if it erases the fragment savings.
Indirect draws now carry world ownership separately from the existing
`INDIRECT_ZBIAS` grouping. External BSP models and world submodels therefore
stay protected even when the z-fighting workaround is disabled, without
changing which draws receive that workaround.

The [META eye-foveation extension](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/meta/meta_foveation_eye_tracked.adoc)
has its own system-support and per-frame validity signals; it does not require
the application to obtain an `XR_EXT_eye_gaze_interaction` action. The imported
XR boundary now accepts a user-permitted eye-mode request without requiring
that separate gaze action to be active. The XR boundary now uses the same
three-frame stability helper as the KHR path, based on META validity. The
eventual renderer callsite must still honor `vr_eye_tracking` and request eye
mode only on appropriate focused frames.
