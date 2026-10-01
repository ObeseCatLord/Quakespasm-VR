# Narrow synchronization diagnosis and repair decision

2026-10-01. Verified main brief for local Astra xhigh. Preserve native pass,
framebuffer, pipeline, task and submission owners. Required desktop qualification
finding, not a new renderer/feature. Production currentb5e7af20; main/master
read-only. Metadata worker owns unrelated fixture files; no production writer.

## Facts, inference and environment

| Claim | Status / checkable evidence |
| --- | --- |
| Native baseline and2.0 emit repeated queue-submit WAW hazards | [verified: independent rendered runs] final-renderer-qualification-2.0-review.md, baseline-desktop-exit.log and desktop-hazard-pass.log under /tmp/qsvr-final-qualification-thchgzi8/gpu/logs. Current clean simulatedXR24-probe result applies to earlier source, not desktop. |
| GPU/runtime/layer | [verified] RTX4090/driver615.71.09, private validation1.4.357, SDL3.4.16. Native desktop; isolated assets and config, no user runtime changes. |
| First hazard access | [verified: callback/GDB] prior attachment store at COLOR_ATTACHMENT_OUTPUT in subpass1 of previous primaryCB; current vkCmdEndRenderPass final layout transition, write_barriers0. First two record calls use cb0/swapchain2 and cb1/swapchain1, both acquiredtrue. Thus those actual swapchain images differ. See desktop-hazard-pass.log. |
| Message lacks image identity | [verified: official VVL source] FirstUseError/SubmitTimeError has a TODO to provide EndRenderPass resource handles: https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/layers/sync/sync_error_messages.cpp. Captured callback pObjects contains queue/CB only. Extra-property settings do not add the missing image. Do not claim direct image identification. |
| Shared image/view identities captured | [verified: GDB] desktop-hazard-identity.log: color_buffers[0]=0xc600000000c6, view0xc800000000c8; current swapchain1=0xbc00000000bc/view0xbf00000000bf; depth0xd5...,MSAA0xcc...,ui_color_buffernull. Handles are diagnostic-run-local. |
| Generated passes retain every target attachment, including unused ones | [verified: main source read] r_passes.c R_CreateGraphicsPasses copies all descriptions and records used_here but does not omit unused descriptions. R_MarkAttachments records color/resolve/input/depth use, not preserve-only indices or special rate attachments. Framebuffers likewise bind the full target list. |
| Desktop MSAA+OIT world/entity passes leave single-sample scene color unused | [verified: native source/active layout] R_CreateScenePasses uses MSAA index2; desktop OIT MAIN has no resolve reference. Early world/entity passes have one MAIN subpass, while single-sample color0 remains declared UNDEFINED->COLOR_ATTACHMENT_OPTIMAL. Prior UI uses/stores color0 at its subpass1; scene color is shared between frames. Other OIT attachments can also be unused in split passes. |
| Unused attachment still executes declared layout transitions | [verified: official spec] https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html explicitly distinguishes ignored load/store from retained transitions. FinalLayout cannot be UNDEFINED. |
| Leading cause | [inference] Early scene EndRenderPass transitions unused shared color0 before a subpass reference/dependency can synchronize it with prior UI store. Actual images2/1 differ, making a previous-swapchain-image conflict unlikely for this first hazard. Exact resource not directly reported by VVL; challenge this inference. |
| Shutdown abort | [verified separate symptom; relationship unknown] desktop-teardown-events.log records native input/video shutdown, SDL window/video destruction, SDL_Quit at exit, then allocator abort in driver library finalization. No vkDestroyDevice/Instance event. Do not prescribe broad cleanup or infer driver fault; first test justified synchronization correction. |

## Options, reuse and current lean

1. **Compact only unused attachment slots at the existing pass compiler.** Reuse
   used_here and target attachment descriptions; retain slots needed by actual
   references, preserve-only references and explicit density/FSR ownership. Store
   one derived logical-to-physical slot map at existing physical_pass_t; remap
   ephemeral subpass refs/preserve lists, framebuffer view lists and clear values
   consistently. Retain every used format/sample/load/store/layout and all native
   passes, pipeline bindings, descriptors, frame slots and submission machinery.
   No new lifetime/policy state or render resource owner. Lean toward this if the
   inference is valid: remove needless transitions instead of serializing them.
   Estimated100–140changedlines in r_passes.c; reopen BEFORE160. Pipeline render-
   pass compatibility and rate-map last-slot assumptions require source review.
2. **Explicit synchronization for unused transitions.** A targeted pre-pass
   image barrier or one global native boundary could be smaller, but retains
   meaningless work and risks over-serialization. Standard outgoing subpass
   dependencies apply layout-transition availability where that subpass uses
   the attachment; an unused attachment has no such reference. A generic outgoing
   edge is not assumed to solve this. Review official VkSubpassDependency/render-
   pass scope rules before choosing it. Existing warp edge is not a reason to
   add one indiscriminately. No queue-idle per frame.
3. **Set unused initialLayout==finalLayout.** Reject without actual-layout proof:
   shared scene color is shader-read after UI, not always COLOR_ATTACHMENT_OPTIMAL.
   Avoid guessing layout state or adding a persistent per-image tracker.
4. **Replace pass system/renderer.** Reject: native state/resource ownership is
   reusable and no evidence justifies replacement.

Before actual coding, main records adopted design/write set/estimate. Minimal
vertical proof: existing native desktop with identical settings, zero current
WAW errors, correct scene output, then clean actual24-probe XR matrix, MSAA1/4,
OIT0/1/2, SSAO on/off and resource transitions. Preserve full-rate/foveation pass
descriptors, pipeline compatibility and borrowed-image ownership. Normal desktop
quit remains an independent requirement, not automatically fixed by validation.

## Review contract

One local Astra, explicitxhigh. Read-only r_passes.c/gl_vidsdl.c/r_ssao.c and native
baseline equivalents, cited logs and official specs. No edits/builds/GPU reruns/
SSH/children or user telemetry exports. Verify before critique. Output<=1000words:
rank options, challenge cause/needed architecture, identify cheapest additional
diagnostic if needed, smallest accepted design/write set/bound and acceptance.
Do not make speculative shutdown cleanup part of this repair. If evidence is
insufficient, say exactly which resource/contract needs proof. Terminal return.
