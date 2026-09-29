# OpenXR foveation after ordinary desktop Vulkan startup

**Backend selection superseded on 2026-09-29:** the user directed that KHR
shading rate be mothballed. The [FB/META sole-route plan](openxr-fb-meta-foveation-2.0-plan.md)
governs the next implementation. This document retains the Astra device-time
readiness and non-FB attachment findings, not its former KHR priority.

Status: Astra Max design accepted; device-readiness adapter and non-FB
attachment correction implemented on `2.0`. Final source review is pending.
Scope: enable existing KHR eye-tracked shading rate after compatible-device late
attachment, and make the existing explicit FB/META development route eligible
when the XR runtime is not present at Vulkan creation. This is a device-time
readiness adaptation, not a new compositor, eye provider, culling algorithm, or
renderer owner. The full migration scope and exclusions remain unchanged.

## User-visible outcome and reference

A desktop-started process on a capable GPU can execute `vr_enable 1` and use
`XR_EXT_eye_gaze_interaction` with the existing vkQuake KHR shading-rate scene
pass when the newly found runtime supplies valid gaze. On GPUs without the
needed extension/feature, it stays full rate. `vr_eye_tracking` gates eye mode;
`vr_foveation 1` alone requests fixed mode, never a default/fallback. On a GPU
with eligible density-map features, an **explicit** `-vk-runtime-foveation`
request may prepare the existing FB/META development path even before the XR
runtime appears. Later runtime capability and valid profile/image contracts
remain necessary; absent/invalid eye tracking restores full rate. A non-FB
runtime must still attach ordinary full-rate VR even if this optional FDM
feature was prepared at device creation. Startup
`-openxr` behavior, desktop effects/SSAO/MSAA and donor GPU selection remain.
Steam Frame streaming/ARM standalone and Beyond 2e/Monado remain targets, not
headset-name allowlists or assumed extension sets.

[Verified in current source] `gl_vidsdl.c` only enumerates KHR shading-rate,
RenderPass2, density-map and offset extensions if `openxr_vulkan_binding` is
already true. `create_renderpass2_core` is selected only for startup OpenXR's
Vulkan1.2 request; ordinary desktop requests Vulkan1.1, so it needs advertised
`VK_KHR_create_renderpass2`. Foveation properties/features and GPU candidates
are also hidden behind live `VRXR_VulkanFoveationSupported()`. The existing
`GL_FoveationRequestedActive` and `GL_DensityFoveationRequestedActive` check
stereo state, user modes, gaze/profile, render extents/MSAA and resource state.
`VRXR_AttachVulkan` owns density-image negotiation and healthy fallback to
ordinary swapchains; device metadata now survives XR rediscovery. No changes
are required in the rate-map math or scene pass compiler for device readiness.

[Verified primary specification] `VK_KHR_fragment_shading_rate` requires
physical-device-properties2 or Vulkan1.1, plus RenderPass2 or Vulkan1.2:
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_fragment_shading_rate.html
RenderPass2's base dependencies are covered by Vulkan1.1:
https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_create_renderpass2.html
Vulkan forbids enabling `fragmentDensityMap` with
`attachmentFragmentShadingRate` (VUID 04483):
https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html
Valve lists FB/META runtime foveation for Steam Frame but does not prove any
particular headset/runtime offers KHR rate support:
https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom
The inherited FB/META image and gaze-contract limits are recorded in
[vulkan-foveation-2.0-plan.md](vulkan-foveation-2.0-plan.md).

Unknown: each release target GPU's full KHR/FDM format and sample-rate matrix,
actual XR runtime capability, and Beyond gaze (not configured). New current-host
probe on 2026-09-29 enumerates NVIDIA RTX4090 and RADV devices, both advertising
`VK_KHR_fragment_shading_rate`, `VK_KHR_create_renderpass2`,
`attachmentFragmentShadingRate=true` and 32 descriptor sets. A direct Vulkan1.1 physical-device probe on this machine additionally confirms
both GPUs expose `R8_UINT` shading-rate attachment plus transfer destination,
array layers, and 1x1/2x2 shading rates at 4x MSAA; neither advertises
`VK_EXT_fragment_density_map`. The existing `openxr_layout_fixture.c` passes
the real donor descriptor and pipeline layout owners with multiview readiness
off and on on this host. This replaces the old
software-only evidence for layouts; it does not prove foveated draw commands,
XR images or headset output. User does live/performance tests.

## Architecture comparison

| Option | Reuse, state and compatibility | Decision lean |
| --- | --- | --- |
| Move current Vulkan capability queries/feature selection to the already-created device boundary independent of XR discovery; keep one selected feature family and all existing render/runtime owners | Reuses pass, rate-map, gaze, FDM image/retirement and device creation code. Corrects the observed gating incompatibility without a second policy owner. Explicit FB development flag resolves runtime-absent uncertainty; default prefers KHR. | Preferred; no extra frame-time allocations or a new render graph. |
| Enable both KHR and FDM features to defer the choice | Vulkan VUID 04483 disallows this combination. | Reject. |
| Recreate Vulkan device when runtime later appears | Would need live texture/mesh/staging/descriptor reconstruction absent from `VID_Restart`, including game continuity. | Keep as separately planned incompatible-device fallback, not routine foveation attachment. |
| Choose FDM automatically on any desktop GPU or hardware vendor | Can remove a working KHR eye path, and current FDM pass is developmental with an unproved borrowed-image/net-cost contract. | Reject. |

## End-to-end implementation contract

1. At desktop device creation (unless `-novr`), enumerate KHR shading-rate,
   RenderPass2 and FDM candidates on the actual donor GPU. Check Vulkan API,
   promoted dependencies, actual feature/property bits, chosen R8/RG8 image
   support and entry points. Reuse current query/selection code and the
   existing device extension array; expand only if capacity requires it. No
   device feature can be asserted merely because an extension is advertised.
2. Preserve the current `-openxr` path. Without a runtime, prepare KHR as the
   default only if its complete GPU/RenderPass2 candidate passes. With an
   explicit `-vk-runtime-foveation`, allow a complete GPU FDM candidate before
   XR discovery; if a runtime was already discovered, retain the existing META
   requirement. Select exactly one Vulkan feature family. If the requested
   development FDM route cannot qualify, the preexisting KHR/full-rate choice
   remains; no fixed-mode switch is performed. A full-rate XR runtime without
   FB/META must not lose VR merely because the persistent device has FDM: pass
   a density request to `VRXR_AttachVulkan` only if both the FDM device feature
   and the newly discovered runtime's FB support are present.
3. Retain the existing `vr_foveation`/`vr_eye_tracking` control, sample-rate,
   stability and action/profile checks at frame time. Desktop remains full rate
   because `stereo_active` is false; later XR adoption uses current renderer
   capabilities without rebuilding VkDevice. A runtime lacking usable gaze or
   the chosen family renders full rate, never fixed fallback.
4. At the end of the full migration, verify exact creation parameters (enabled extension names/features, real
   `vkCreateRenderPass2`/`vkCmdSetFragmentShadingRateKHR` or FDM entry points)
   through production-boundary checks. Exercise default desktop, desktop then
   XR gaze, explicit FDM with runtime absent/present, unsupported extension,
   `-novr`, lost instance/rediscovery, MSAA and invalid gaze. Check real Vulkan
   device setup on an accessible capable GPU when available. State seams
   honestly: actual donor six-set layouts and KHR format/rates now pass locally,
   but foveated draws, HMD/XR images and FDM GPU execution are not proven here.
   The user has deferred all further builds and tests until implementation of
   the full goal is finished. Windows/ARM and live/performance checks remain
   user-deferred.

Estimate: under150 production lines inside `gl_vidsdl.c`, no new owner or
policy state; reopen if the change duplicates Vulkan selection, alters desktop
passes, needs scene/image rewrites or exceeds200 lines. Temporary explicit
FDM preparation is not proof of FB/META performance or readiness on Frame.

## Senior review brief

Solo maintainer, no enterprise ceremony. Astra Max should verify the Vulkan
mutual-exclusion/dependency rules and actual source gates, then challenge the
simplest safe device-time selection when XR is absent. Rank desktop regression
risk, FB/META uncertainty, KHR device extensions, and whether a smaller
adapter is possible. Do not review the whole migration, shader math, network or
asset systems. Return a prioritized adopt/adapt/reject critique with file/line
and primary-source evidence, no edits or tests. Main will synthesize before
production coding.


## Astra Max design disposition

Local reviewer Sartre (`gpt-6-astra`, max; effective settings verified) checked
current source and official Vulkan/OpenXR/Valve references. Main spot-checked
the load-bearing [`vkGetDeviceProcAddr` core-version rule](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceProcAddr.html)
and the early non-FB rejection before the existing optional density retry.

| Finding | Disposition |
| --- | --- |
| A prepared FDM device passed directly to non-FB `VRXR_AttachVulkan` prevents ordinary VR, before density fallback | **Adopt.** Gate the *per-attachment request* with current runtime FB capability, preserving the VkDevice feature for future rediscovery. Test full-rate attachment without FB after speculative preparation. |
| Desktop requests Vulkan1.1; a Vulkan1.2+ GPU alone does not authorize core RenderPass2 dispatch | **Adopt.** Keep `create_renderpass2_core` tied to requested application version and GPU; enable/use KHR extension/entry points on desktop. |
| Partial removal of XR discovery gates still blocks KHR/FDM candidates | **Adopt.** Audit enumeration, function pointer, usability, property, feature, candidate and selection gates in the existing device owner; retain actual Vulkan format/feature checks, sample-rate query at the existing renderer boundary and disabled offset path. |
| FDM preparation is not eye-foveation proof at default 4x MSAA or an unqualified borrowed-map contract | **Adopt as limit.** Do not infer Frame performance/visual readiness. Keep explicit development flag, VR eye toggle and full-rate fallback; fixed mode only on explicit request. |
| New capability owner or routine VkDevice rebuild | **Reject.** Reuse existing renderer and backend owners. Incompatible-device reconstruction stays its separately required later phase. |

The KHR route can reach the existing gaze/map/frame path after compatible
adoption, but that is source-based until real foveated opaque draws/XR images are
verified. Native host GPUs can validate KHR device creation and layouts; neither
advertises FDM. Subsequent code review must examine both desktop and late VR
paths, with no new render graph or GPU-state owner.

## Implemented adapter

`GL_InitDevice` now prepares KHR shading-rate and RenderPass2 eligibility on
the actual Vulkan device before OpenXR discovery, provided VR was not excluded
with `-novr`. The same point probes FDM GPU eligibility for an explicit
`-vk-runtime-foveation` request. The selected KHR or FDM feature remains
exclusive in VkDevice creation. Desktop still requests Vulkan1.1 and uses the
KHR RenderPass2 extension and dispatch when selected. At XR attachment,
`GL_OpenXRAttach` requests borrowed density maps only when both the prepared
device and current runtime support FB foveation. This lets a prepared FDM
device attach ordinary full-rate VR to a non-FB runtime. The existing
`vr_eye_tracking` and `vr_foveation` controls remain the frame policy; this
change does not select fixed foveation by default.

The new `openxr_enable_fixture.c` cases describe a desktop-prepared FDM device
attaching first to a non-FB runtime and later to an FB runtime without losing
its device feature. These use the source renderer boundary with simulated
runtime and empty GPU resources. Earlier local builds and checks predate the
user's latest testing deferral; no further checks will be run until full
implementation is complete.
