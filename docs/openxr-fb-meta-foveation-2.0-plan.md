# FB/META foveation as the sole OpenXR foveation route

Status: implementation plan, following the user's 2026-09-29 decision to
mothball Vulkan KHR attachment shading rate. The complete vkQuake migration
goal remains active. No builds or tests until implementation is complete, per
the latest user instruction.

## Intended behavior and evidence

OpenXR VR selects the existing runtime-owned `XR_FB_foveation` / `XR_META_foveation_eye_tracked`
path when the actual Vulkan device and current runtime meet the complete
contract. `vr_eye_tracking` is the user toggle for eye-tracked mode;
`vr_foveation 1` is the only way to request fixed mode. An absent extension,
unavailable/invalid gaze, unsupported graphics configuration, or runtime
failure keeps full-rate stereo without selecting fixed foveation. Desktop
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
The current single-path incompatibilities are concrete: device selection
prefers KHR except behind `-vk-runtime-foveation`; runtime-absent desktop
creation cannot ready FDM; the FDM scene is rejected at the donor default
`vid_fsaa 4`; and borrowed image assumptions (RG8 format, layout/readiness,
size) have not been qualified against intended runtimes. The local NVIDIA
RTX4090 and RADV iGPU do not advertise Vulkan FDM, so this host cannot prove
FDM execution. Beyond 2e/Monado and PC-streamed Frame may therefore render
full rate even when eye tracking is accessible. That is an explicit limitation
of the user's one-method decision, not a reason to silently re-enable KHR.

## Architecture decision

| Option | Reuse and duplicated state | Decision |
| --- | --- | --- |
| Keep the current FB/META backend and vkQuake pass compiler; change device-time selection and qualify the FDM image/MSAA boundary | Reuses existing session, gaze, profile, scene and fallback owners. No second runtime state machine. | Adopt. |
| Keep KHR active as a hidden automatic fallback | Contradicts the requested one-method route and makes device feature selection ambiguous. | Reject; compile dormant source if useful, but do not enable or select it. |
| Replace vkQuake's renderer with a new FDM compositor | Duplicates passes, culling, graphics settings and image lifetime. | Reject. |
| Recreate VkDevice on every XR attach | Needed only for an incompatible runtime/GPU/API/extension contract; expensive live asset recreation is not a normal foveation prerequisite. | Separate incompatible-device project. |

## Implementation slices and acceptance contract

1. **Mothball KHR selection.** Disable the KHR shading-rate device feature,
   extension, rate-map and pass activation in normal builds. Preserve dormant
   source as a reference pending cleanup; remove docs that call it the
   production baseline. Do not add a KHR fallback. Keep desktop and unqualified
   XR at full rate. This is a policy change at the existing device owner, not
   a parallel foveation owner.
2. **Prepare FDM on a capable VkDevice.** At normal desktop or startup XR
   creation, query Vulkan1.1, RenderPass2, multiview, actual FDM feature,
   non-subsampled-image feature, RG8 usage/array layers and entry points on
   the chosen GPU. Enable the FDM device feature only when the complete
   candidate passes, independent of whether XR has already been discovered.
   Device creation remains through the existing direct or enable2 owner;
   the actual creation metadata is retained for later XR adoption. At each
   attachment, request density images only if the current runtime offers FB
   support; otherwise attach ordinary stereo. A runtime without META can use
   fixed mode only on explicit user request; eye mode stays full rate.
3. **Make the runtime path useful at ordinary graphics settings.** Remove the
   unconditional `vid_fsaa >= 2` exclusion only after mapping the density
   scene's color/depth/resolve attachments and pass compatibility for the
   actual sample count. Preserve 4x desktop/VR MSAA and SSAO. If a Vulkan
   configuration does not support FDM+MSAA, keep MSAA and render full rate;
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
