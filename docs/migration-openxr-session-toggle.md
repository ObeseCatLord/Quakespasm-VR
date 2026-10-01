# OpenXR selection, late attachment and session recovery

The `vr_enable 0|1` command queues a transition at the next renderer frame
boundary. `0` releases VR input, retires borrowed images through the existing
renderer callback and destroys the session. Desktop keeps the existing Vulkan
device/window. `1` attaches explicitly; two commands in one batch resolve to
the last requested state. `-novr` disables selection. EXITING and failed attempts
never retry automatically.

`-openxr` startup retains the enable2 path, including runtime GPU selection and
real runtime-wrapped Vulkan creation. Ordinary desktop startup retains donor GPU
selection and does not require an XR runtime. On supported core1.1 devices,
multiview and a finite platform interop extension set are enabled before device
creation so an explicit later selection can qualify that existing device.
Foveation optimization is not enabled by this readiness policy.

After desktop startup or instance loss, `vr_enable 1` can discover a fresh XR
instance using original `XR_KHR_vulkan_enable`. It compares actual creation
metadata, API minimum, required enabled extension names and runtime-selected GPU
before session creation. enable2 runtime-added parameters are captured through
real Vulkan creation dispatch; the renderer keeps this metadata across XR
teardown. Each attachment requalifies, and fresh adoption reinstalls the current
queue mutex callbacks. Failed qualification leaves desktop usable and another
attempt requires an explicit command. A runtime exposing only enable2, a
changed/incompatible GPU/requirements or real device loss still needs startup
selection or a future live reconstruction implementation.

One renderer/device/asset owner and one XR session/frame/input owner remain.
The renderer joins and retires work at the existing boundary; no parallel
render/session state machine was introduced. View dimensions are recalculated
after `GL_BeginRendering` for a successful switch.

See the [late attachment plan and Astra disposition](openxr-late-binding-2.0-plan.md)
and earlier [session recovery checks](openxr-session-recovery-2.0-plan.md).
Linux Make and bounded backend/command/creation checks pass. Real headless
Vulkan creation/capture passes on SwiftShader; the real donor-layout test skips
because that driver has four descriptor sets and stereo needs six. XR/driver
spies and empty render resources do not prove loaded-scene/asset continuity or
real borrowed-image GPU completion. These are historical component-check
results, not qualification of the current tree. Live headset/gaze/performance and Windows builds remain user-deferred;
Linux ARM software qualification belongs to the final consolidated pass.
General live incompatible-device reconstruction and device-loss recovery are
explicitly deferred by the current scope disposition. The
[late-foveation readiness adapter](openxr-late-foveation-2.0-plan.md) is already
source-integrated; actual foveated draws and borrowed-image behavior remain
software qualification requirements after all implementation, not permission
to introduce a second device/session owner.

## Current source reconciliation: VR-001 and XR-002

2026-09-30. Main traced actual command/frame/retirement/input consumers on2.0;
no production edits or execution-based checks in this reconciliation.

| Owner boundary | Current source behavior | Remaining qualification |
| --- | --- | --- |
| Selection to renderer | gl_vidsdl.c GL_OpenXREnable_f queues explicit intent; GL_OpenXRApplySessionChange joins and releases input before detach. GL_OpenXRAttach admits original/adopted device metadata, queue callbacks and two-layer attachment, with one attempted latch and desktop fallback. | Actual startup/late attach/healthy disable/explicit session and instance recovery; failed qualification retains usable desktop. |
| Runtime state to frame | vr_openxr.cpp poll_events handles READY/STOPPING/LOSS_PENDING/EXITING, instance/reference/mask events. VRXR_BeginFrame clears output, waits/begins, locates at predicted display time, and acquires/waits only for renderable frames. | Runtime event order, invalid poses, focus and reference continuity; software facilities are not headset proof. |
| Borrowed image to submission | begin_images acquires each actual chain once; wait_chain retries the same image with a bounded timeout budget. release_chain requires acquired+waited. GL_EndXRFrame joins native submission before marking both eye accesses; end_frame releases then submits two projection views or an empty frame. | Real borrowed-image/queue ordering across normal, partial and failed frames. No quad views; one array chain is not acquired twice. |
| Retirement to native owners | destroy_session_resources invokes GL_OpenXRRetireImages before destroying swapchains. Native GL_DestroyRenderResources calls GL_WaitForDeviceIdle and destroys framebuffers/image views. Session-only teardown preserves qualified creation ownership; instance loss abandons runtime state. | Actual GPU completion and resource lifetime through detach/loss/loading/restart; no incompatible-device reconstruction gate. |
| Input/error consumer | Host_Frame consumes the last completed sample before bindings. VR_InputCommands rejects absent/unfocused input with native release/neutral gates. Host_Error reaches SCR_AbortXRFrame, native idle/join, backend abort and view restoration. | Held inputs, loading/error/reference/profile changes and no stale motion or click-through. |

These source paths reconcile the old source-mapped inventory statuses; they do
not certify inherited coordinate/pose parity, actual controller behavior,
rendered output or final Linux/ARM acceptance. Those remain their own owner and
software qualifications. Live headset trials remain user-deferred and outside
completion. No new session/renderer/input state machine is added.
