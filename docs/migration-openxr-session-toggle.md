# OpenXR session toggle on the startup Vulkan binding

The `vr_enable 0|1` console command queues a transition at the next renderer
frame boundary. `0` releases VR-owned input, retires renderer references to
borrowed swapchain images through the existing OpenXR callback, and destroys
the OpenXR session. Desktop rendering then uses vkQuake's existing Vulkan
device and window. `1` creates a new session using that same Vulkan binding.
Two commands in one command batch resolve to the last requested state.

The command requires `-openxr` at startup, because the runtime chooses the
Vulkan physical device during bootstrap. It cannot turn an ordinary desktop
Vulkan device into an XR-compatible binding, nor recover a lost OpenXR instance
or device. After session-only loss or EXITING, the game returns to desktop and
an explicit `vr_enable 1` can retry the retained binding. Every new attachment
checks the current headset system, runtime API minimum and selected GPU. Changed
hardware, instance loss or failed session destruction requires a restart to
rediscover the binding. EXITING never starts an automatic retry. See the
[session recovery plan and checks](openxr-session-recovery-2.0-plan.md).

The renderer still owns resource creation and retirement; the OpenXR backend
still owns session state. No parallel render or session state machine was added.
The screen path recalculates the view after `GL_BeginRendering`, so a successful
switch uses the new dimensions in its first rendered frame.

Linux SDL3 build and bounded backend, renderer command/retirement, camera and
input-helper checks pass. These use simulated runtime or prepared boundaries;
they do not prove GPU completion or complete renderer/input/runtime integration.
Live headset, Windows and ARM results are not claimed. The user performs live
testing later, outside the current implementation goal.
