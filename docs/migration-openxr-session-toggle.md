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
or device. If the runtime reports a terminal stop, restart with `-openxr` to
rediscover the system and binding. This is a deliberately bounded session
lifecycle step, not full hot-connect or runtime-loss recovery.

The renderer still owns resource creation and retirement; the OpenXR backend
still owns session state. No parallel render or session state machine was added.
The screen path recalculates the view after `GL_BeginRendering`, so a successful
switch uses the new dimensions in its first rendered frame.

Linux SDL3 debug build and `git diff --check` passed. No live runtime toggle,
headset, Windows, or ARM result is claimed here. Live behavior and recovery
remain separate release verification gates.
