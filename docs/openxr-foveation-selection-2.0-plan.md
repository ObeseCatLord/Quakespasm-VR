# Prefer FB/META foveation with KHR capability fallback

**Current status, 2026-09-30:** the [accepted defaults decision and source
checkpoint](openxr-foveation-defaults-2.0-plan.md) supersedes the earlier
borrowed-image release blocker and development-switch policy below. Automatic
FB/META preference is source-integrated for complete candidates. The existing
[application density-offset route](openxr-density-offsets-2.0-plan.md) and native
MSAA remain; no new renderer or resource owner was introduced. Runtime metadata,
layout and producer readiness remain explicit interoperability assumptions,
rather than universally certified guarantees. Final Linux/ARM software checks
remain deferred until implementation ends; the user performs live qualification.

Quad views are excluded. Exactly two PRIMARY_STEREO views and conservative
peripheral geometry remain. The complete vkQuake migration goal is still active.

## Current behavior and evidence

At VkDevice creation, discovered OpenXR eye support and complete GPU/runtime,
attachment/sample and offset support allow FB/META to take priority without a
development switch. Explicit fixed mode can use its existing FB path. Ordinary
desktop creation retains KHR preference where available; otherwise it can
prepare FDM for later runtime discovery. KHR is an alternative at device
creation, not a live fallback on an FDM device. Failure after FDM selection
recovers to full-rate stereo on that device.

`vr_eye_tracking` remains the user toggle. `vr_foveation 1` explicitly requests
fixed mode; missing or invalid gaze never selects it. Graphics and cvars stay
with vkQuake. No headset-name allowlist or new metadata query was added.

Valve's [Steam Frame custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
recommends these six extensions: `XR_FB_foveation`,
`XR_FB_foveation_configuration`, `XR_FB_foveation_vulkan`,
`XR_FB_swapchain_update_state`, `XR_META_foveation_eye_tracked`, and
`XR_META_vulkan_swapchain_create_info`; it separately lists
`XR_EXT_eye_gaze_interaction`. The [FB Vulkan flag](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainCreateFoveationFlagBitsFB.html)
requires an enabled `VK_EXT_fragment_density_map` device extension. The
[borrowed image structure](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainImageFoveationVulkanFB.html)
returns a Vulkan image and dimensions paired with an XR swapchain image.
The [META eye state](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFoveationEyeTrackedStateMETA.html)
reports per-eye NDC centers and validity. The full eye profile additionally
depends on both FB foveation and configuration extensions; extension names
alone do not prove a usable device/image/profile contract.

Existing code already owns FB/META discovery, runtime profiles, borrowed
image enumeration, multiview density-scene pass, full-rate protected pass and
depth replay (`vr_openxr.cpp`, `gl_vidsdl.c`, `r_passes.c`, `gl_rmain.c`).
The published FB Vulkan interface does not explicitly establish the borrowed
image's RG8 format, layer count, layout or producer readiness. The defaults
decision accepts the concrete public application convention with these limits;
capability probes and successful view creation do not attest to runtime
metadata. The density pass supports native default MSAA without lowering
quality. Actual performance and target-runtime compatibility remain unmeasured.
The local NVIDIA
RTX4090 and RADV iGPU do not advertise Vulkan FDM, so this host cannot prove
FDM execution; both do advertise the KHR candidate, so an OpenXR runtime with
a working `XR_EXT_eye_gaze_interaction` action can still support eye foveation.
Beyond 2e/Monado and PC-streamed Frame may therefore use KHR rather than
FB/META. A runtime's extension list never substitutes for actual GPU support.

## Historical implementation and review record

The following sections preserve earlier design and source-review history.
Development-switch requirements and release-blocker dispositions are superseded
by the defaults decision linked above; retained ownership, capability, sample,
optional-mode and error-recovery constraints still apply. No historical test
record below represents a check of the current source checkpoint.

## Architecture decision

| Option | Reuse and duplicated state | Decision |
| --- | --- | --- |
| Keep current FB/META and KHR adapters under the existing policy; select exactly one feature family at VkDevice creation | Reuses session, gaze, profile, scene and pass owners. No second runtime state machine. Vulkan forbids both features on one device. | Adopt; prefer qualified FB/META and use KHR when it cannot be selected. |
| Replace vkQuake's renderer with a new FDM compositor | Duplicates passes, culling, graphics settings and image lifetime. | Reject. |
| Recreate VkDevice on every XR attach | Needed only for an incompatible runtime/GPU/API/extension contract; expensive live asset recreation is not a normal foveation prerequisite. | Separate incompatible-device project. |

## Implementation slices and acceptance contract

1. **Select one device feature family.** Probe both Vulkan candidates at device
   creation. The unqualified FB/META path requires
   `-vk-runtime-foveation`; only then can startup XR prefer it when the runtime
   advertises eye-profile support, the GPU has FDM and current settings permit
   its density pass.
   Read the existing `r_width`/`r_height` settings alongside FSAA before
   device selection; otherwise a saved reduced render size is applied only
   after an FB/META device has already precluded KHR. Later command changes
   still cannot change the selected feature family without device recreation.
   Otherwise select KHR if the GPU qualifies. Ordinary desktop creation favors
   KHR when present because no runtime has been discovered yet; explicit
   `-openxr` startup can choose FB/META only with the development switch.
   If KHR is absent, the switch can retain FDM device readiness for later
   runtime discovery; without it, keep full-rate rendering. A runtime/profile
   failure after selecting FDM cannot switch to KHR on the same VkDevice;
   ordinary full-rate VR remains until a future device reconstruction or app
   restart. Never quietly switch to fixed foveation.
2. **Prepare FDM on a capable VkDevice.** At normal desktop or startup XR
   creation, query Vulkan1.1, RenderPass2, multiview, actual FDM feature,
   non-subsampled-image feature, RG8 usage/array layers and entry points on
   the chosen GPU. Enable the FDM device feature only when the complete
   candidate passes and the selection policy above chooses it, independent
   of whether XR has already been discovered.
   Device creation remains through the existing direct or enable2 owner;
   the actual creation metadata is retained for later XR adoption. At each
   attachment, request density images only if the current runtime offers FB
   support; otherwise attach ordinary stereo. A runtime without META can use
   fixed mode only on explicit user request; eye mode stays full rate.
3. **Make the runtime path useful at ordinary graphics settings.** The current
   `r_passes.c` density scene assumes three attachments with one sample, while
   the ordinary vkQuake scene uses a multisampled color attachment and a
   single-sample resolve target at `vid_fsaa 4`. Adapt the existing density
   scene to the same multisampled color/depth plus resolve topology: clear and
   store MSAA color, resolve to scene color, then load the MSAA color in the
   protected full-rate pass while clearing/replaying authoritative depth.
   Matching resolve references keep world pipelines compatible across the
   density and ordinary scene passes. Remove the unconditional MSAA exclusion
   only with this adaptation. Preserve 4x desktop/VR MSAA and SSAO. Explicit
   per-sample supersampling can stay full rate if it defeats density savings.
   If a Vulkan
   configuration does not support FDM+MSAA, keep MSAA and select KHR where
   possible, otherwise render full rate;
   never silently lower graphics quality to obtain foveation. The narrowest
   proof is one borrowed density image used by a two-view coarse-world pass,
   followed by authoritative full-rate depth, protected world/entity/UI and
   unchanged final color at both 1x and 4x MSAA. Reopen this architecture
   decision if the pass adaptation duplicates existing render policy or
   requires a new render graph.
4. **Qualify the borrowed image boundary.** Validate the image/view format,
   dimensions, array layers and required usage/layout against the runtime's
   returned image and the Vulkan feature contract. Keep XR-owned images alive
   through their swapchain lifetime and retire all app views before detaching.
   Preserve the existing failed-density-to-ordinary-stereo retry. Do not
   assume that a successful `xrEnumerateSwapchainImages` or `vkCreateImageView`
   alone proves the image can be used with this renderer. A 1-layer density map
   is legal for Vulkan multiview, but this renderer currently creates a 2-layer
   view to allow distinct eye patterns. Do not infer its layer count from the
   color swapchain's `arraySize`. Establish the producer-completion and initial
   layout contract for the borrowed image before the density pass; a device
   subpass dependency does not make a host-read map ready. On qualification
   failure, rebuild using the existing ordinary scene topology rather than
   merely drawing no world into a still-active density render pass.
5. **Guard user behavior.** Keep `vr_eye_tracking` and `vr_foveation` as shared
   VR options. Eye mode requires META profile, focus, current valid eye state
   and the existing stability policy. Invalid or lost gaze restores the off
   profile immediately, with no fixed fallback. Fixed mode is only requested
   by `vr_foveation 1`. Culling remains conservative eye-union PVS/frustum;
   FDM reduces fragment cost and never removes peripheral geometry.
6. **End-of-goal verification.** When implementation is finished, run a
   consolidated Linux build and the existing renderer/backend fixtures, plus
   genuine GPU/runtime scene checks where available. The user will run live
   headset, eye-tracking, Windows and performance checks later. Linux ARM
   software build/packaging qualification remains an end-of-implementation
   engine task via the isolated Foundry build. No
   performance improvement or Beyond/Frame compatibility claim follows from
   extension discovery or a simulated image alone.

Expected first production slice is confined to the existing device-selection
owner and a small backend/attachment fixture update. MSAA and borrowed-image
qualification are distinct renderer slices with separate review. Any need for
new owners or broad pass rewrites requires revisiting this plan first.

## Static review disposition for the MSAA slice

An Astra Max source review found two concrete issues after the first pass
adaptation. Both were accepted and corrected in the existing owners:

| Finding | Correction and limit |
| --- | --- |
| A single `world_depth_replay_pipeline` was built for the standard scene and could not bind in WBOIT/MBOIT density replay. | Create one pipeline per main-pass variant, retaining the existing same-variant alternative-instance mechanism. Select by `cbx->pipeline_variant` in both direct world draws and indirect brush draws; destroy each variant with the other world pipelines. This is an existing pass-owner extension, not a second render graph. |
| Density eligibility read `supersampling` from the prior resource generation before `GL_CreateColorBuffer` refreshed it. | Evaluate current `vid_fsaa`, `vid_fsaamode` and `sampleRateShading` capability conservatively before resource creation. A requested but unsupported multisample count can disable optional FDM, without changing graphics quality. |

The targeted follow-up Astra review accepted the direct draw, pass binding,
pipeline creation/destruction and current-setting corrections; it flagged the
indirect caller as outside its scope. Main inspected and updated that caller.
This remains a **static** disposition: no 4x FDM draw, OIT/SSAO output, borrowed
XR image or gaze-off recovery has been verified on hardware.

## Astra borrowed-image review disposition

A second local Astra Max review checked the existing XR, Vulkan image-view and
render-pass owners against official [OpenXR returned-image](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainImageFoveationVulkanFB.html),
[Vulkan view](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)
and [density-map read-time](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassFragmentDensityMapCreateInfoEXT.html)
rules. Meta's [native Vulkan FFR guide](https://developers.meta.com/horizon/documentation/native/android/os-fixed-foveated-rendering/)
shows a two-layer color swapchain and the same borrowed-image enumeration but
does not specify the returned density image's format, layer count or initial
layout. The [open Khronos specification issue](https://github.com/KhronosGroup/OpenXR-Docs/issues/102)
independently records the missing format query/guarantee. Valve's Frame guide
recommends the extension family without specifying these Vulkan image details.

| Finding | Disposition |
| --- | --- |
| RG8 and two density layers are assumed from a handle plus extent. The structure supplies neither format nor layer count; a successful image-view call cannot certify the VUIDs. | **Adopt as release blocker.** Keep existing XR and renderer owners, document the required target-runtime contract, and qualify it through vendor documentation/source or the deferred native runtime checks before claiming Frame/Beyond behavior. Do not add a headset allowlist or pretend the Vulkan format query describes the borrowed image. |
| A static map view is host-read at render-pass recording. `xrUpdateSwapchainFB` and a device-stage dependency alone do not prove the runtime's producer has finished or that the image is in `FRAGMENT_DENSITY_MAP_OPTIMAL_EXT`. | **Adopt as release blocker.** Establish update/acquire readiness and layout on the actual runtime path. If unavailable, use ordinary stereo/KHR selection on a compatible device. |
| `fragmentDensityMapDynamic=false` and view flags zero. | **Reject as a correctness defect on its own.** Static mode is valid when readiness precedes recording. Enabling the dynamic view flag without the device feature would itself violate Vulkan. Optional dynamic/`VK_EXT_fragment_density_map2` work is a later latency optimization only after the image contract is qualified. |
| Off profile with compiled density pass. | **Adapt.** Off can render full-rate color, but the pass still accesses the borrowed image. A missing or unsafe image requires the existing ordinary pass topology, not an empty density draw. |

This is a limitation in the published interface, not proof that Frame's or
Meta's runtime returns an incompatible image. No runtime or headset execution
was performed for this review, as requested.

The release-blocker disposition is now enforced at device selection: without
`-vk-runtime-foveation`, FDM cannot preempt a usable KHR device or be selected
merely because KHR is unavailable. The switch exposes the existing FB/META
path for qualification, not for a default release configuration. Once a target
runtime's image contract is established, remove the switch for that qualified
route and revisit the FB/META preference under the same single-device policy.
The [Khronos runtime inventory](https://github.khronos.org/OpenXR-Inventory/runtime_extension_support.html)
currently lists neither `XR_FB_foveation_vulkan` nor
`XR_META_foveation_eye_tracked` for desktop Monado. This is a published
self-reported capability snapshot, so the installed runtime must still be
queried at attachment.


## Official documentation recheck, 2026-09-29

Re-read [Valve's actual custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
(standalone Linux ARM64, six FB/META recommendations, EXT gaze) and OpenXR1.1.63's
[borrowed-image structure](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrSwapchainImageFoveationVulkanFB.html).
The latter still supplies a compatible image plus width/height, without explicit
format/layer/layout metadata. The Khronos [format-clarification issue102](https://github.com/KhronosGroup/OpenXR-Docs/issues/102)
is still open. Vulkan's [FDM sample](https://docs.vulkan.org/samples/latest/samples/extensions/fragment_density_map/README.html)
uses RG8 as an example of a supported format; required GPU RG8 support does not
by itself identify a runtime-owned image's format. No new inference qualifies
the borrowed-image contract here. Existing fallback/protected full-rate behavior
and user exclusions remain; no FDM tests or policy changes in this checkpoint.


Valve primary-source follow-up: its
[Unity foveation feature](https://github.com/ValveSoftware/Unity/blob/main/com.valvesoftware.openxr.utils/Runtime/Features/ValveOpenXRFoveatedRenderingFeature.cs)
requests the same six FB/META extensions and exposes level, eye mode and valid
per-eye center queries. Its Vulkan image work is delegated to UnityOpenXR
imports, so it supplies no additional borrowed-image format/layer/layout
contract. Its [package documentation](https://github.com/ValveSoftware/Unity/blob/main/com.valvesoftware.openxr.utils/Documentation~/index.md)
notes a Unity2022.3 MSAA limitation; that is not evidence to disable vkQuake
MSAA or its native task graph. No production foveation policy changes or tests.
