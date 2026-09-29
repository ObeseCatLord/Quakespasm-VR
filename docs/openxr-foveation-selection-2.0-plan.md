# Prefer FB/META foveation with KHR capability fallback

Status: implementation plan amended after the user's clarification on
2026-09-29: favor Valve's FB/META route where it can work, while retaining KHR
shading rate where FB/META cannot. Quad views are excluded from 2.0 after the
[Frame/large-map assessment](openxr-quad-views-2.0-assessment.md). The complete
vkQuake migration goal remains active. No further builds or tests until
implementation is complete, per the latest user instruction.

## Intended behavior and evidence

OpenXR VR prefers the existing runtime-owned `XR_FB_foveation` /
`XR_META_foveation_eye_tracked` path when the actual Vulkan device, startup
runtime and current render setting can use it. When FB/META cannot be selected,
a capable device uses the existing KHR attachment shading-rate path.
`vr_eye_tracking` is the user toggle for eye-tracked mode;
`vr_foveation 1` is the only way to request fixed mode. An absent extension,
unavailable/invalid gaze, unsupported graphics configuration, or runtime
failure keeps full-rate stereo if the selected device backend is unusable,
without selecting fixed foveation. Desktop
graphics and cvars stay with vkQuake. No headset-name allowlist.

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
The current FB priority incompatibilities are concrete: device selection
prefers KHR except behind `-vk-runtime-foveation`; runtime-absent desktop
creation cannot ready FDM; the FDM scene is rejected at the donor default
`vid_fsaa 4`; and borrowed image assumptions (RG8 format, layout/readiness,
size) have not been qualified against intended runtimes. The local NVIDIA
RTX4090 and RADV iGPU do not advertise Vulkan FDM, so this host cannot prove
FDM execution; both do advertise the KHR candidate, so an OpenXR runtime with
a working `XR_EXT_eye_gaze_interaction` action can still support eye foveation.
Beyond 2e/Monado and PC-streamed Frame may therefore use KHR rather than
FB/META. A runtime's extension list never substitutes for actual GPU support.

## Architecture decision

| Option | Reuse and duplicated state | Decision |
| --- | --- | --- |
| Keep current FB/META and KHR adapters under the existing policy; select exactly one feature family at VkDevice creation | Reuses session, gaze, profile, scene and pass owners. No second runtime state machine. Vulkan forbids both features on one device. | Adopt; prefer qualified FB/META and use KHR when it cannot be selected. |
| Replace vkQuake's renderer with a new FDM compositor | Duplicates passes, culling, graphics settings and image lifetime. | Reject. |
| Recreate VkDevice on every XR attach | Needed only for an incompatible runtime/GPU/API/extension contract; expensive live asset recreation is not a normal foveation prerequisite. | Separate incompatible-device project. |

## Implementation slices and acceptance contract

1. **Select one device feature family.** Probe both Vulkan candidates at device
   creation. Startup XR prefers FB/META when the runtime advertises eye-profile
   support, the GPU has FDM and current settings permit its density pass.
   Otherwise select KHR if the GPU qualifies. Ordinary desktop creation favors
   KHR when present because no runtime has been discovered yet; explicit
   `-openxr` startup can choose FB/META. If KHR is absent, retain FDM device
   readiness where eligible for later runtime discovery. A runtime/profile
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
   assume that a successful `xrEnumerateSwapchainImages` alone proves the
   image can be used with this renderer.
5. **Guard user behavior.** Keep `vr_eye_tracking` and `vr_foveation` as shared
   VR options. Eye mode requires META profile, focus, current valid eye state
   and the existing stability policy. Invalid or lost gaze restores the off
   profile immediately, with no fixed fallback. Fixed mode is only requested
   by `vr_foveation 1`. Culling remains conservative eye-union PVS/frustum;
   FDM reduces fragment cost and never removes peripheral geometry.
6. **End-of-goal verification.** When implementation is finished, run a
   consolidated Linux build and the existing renderer/backend fixtures, plus
   genuine GPU/runtime scene checks where available. The user will run live
   headset, eye-tracking, Windows/ARM and performance checks later. No
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
