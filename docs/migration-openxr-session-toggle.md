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
