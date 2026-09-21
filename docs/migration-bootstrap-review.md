# Donor Vulkan bootstrap review and disposition

2026-09-20. Scope: the first renderer-owned Vulkan/OpenXR initialization seam on
`2.0`, relative to `c2495dd4`. This is a prerequisite for P1, not completion of
VR gameplay, session submission or the early multiview proof.

The main agent reviewed the existing donor/backend boundaries and delegated only
`Quake/gl_vidsdl.c` and `Quake/glquake.h` implementation to one Terra worker.
Main separately corrected the reused backend's version handling and ported its
existing fault-case fixture. Main reviewed and refined the worker's output before
one Astra (`gpt-6-astra`, verified `xhigh`) read-only code/design audit. Effective
model/effort verification used metadata only; no operational telemetry is copied
here. Main verified both Astra findings in source, applied corrections and
obtained a bounded closure review.

## Architecture retained

The adapter calls the existing runtime discovery/creation APIs from vkQuake's
instance/device startup. vkQuake retains its queue, feature/extension chains,
resource ownership, tasks and desktop renderer. The runtime selects the XR GPU;
`-device` retains donor behavior in desktop mode. No session state machine,
renderer, transport or global recovery system was added.

The minimal adapter was preferred over importing the old `vk_renderer.cpp`
because only instance/device selection is incompatible at this stage. Existing
resource and task owners remain reusable. Three renderer fields record bootstrap
availability, multiview capability and view limit; they do not own XR sessions or
claim stereo rendering. The existing runtime still owns session/frame state.

## Disposition

| Finding / recommendation | Verification and disposition |
|---|---|
| Worker selection rejected runtime minimums above Vulkan 1.1 even when the loader could support them. | **Corrected by main.** Raise the preferred version to the runtime minimum before conservative tested-maximum/loader clamping. The actual selected GPU must independently meet the runtime minimum. |
| Loader support alone cannot establish the instance/device's core 1.1 feature support. | **Corrected by main.** Track the selected instance version and actual XR GPU version; use core or KHR physical-device queries as applicable. Keep ordinary desktop feature negotiation on the donor path. |
| Early errors could leave discovered runtime state outside the successful-device shutdown guard. | **Corrected by main.** Main-thread video shutdown covers prepared/bound runtime state, not only successful device creation. |
| Windows fatal startup errors bypass `Host_Shutdown`/`VID_Shutdown`. | **Adopted from Astra.** The existing Windows main-thread `Sys_Error` branch now calls idempotent `VRXR_Shutdown` while Vulkan/SDL remain alive, before dialog/exit/SDL teardown. This is one error-path hook, not another lifecycle owner. Astra's closure source-check found no remaining issue in this bootstrap scope. |
| Newly explicit instance destruction could leave a live debug messenger. | **Adopted from Astra.** Resolve `vkDestroyDebugUtilsMessengerEXT` before creation; destroy and clear the messenger before `vkDestroyInstance` in the existing failure helper. Astra confirmed the corrected ordering. |
| Imported FB/META chain failed C++ compilation because FB uses mutable `next`. | **Corrected during consolidated checks.** Build the FB tail first and prepend META. Same structures/flags, no discarded const qualifier or activation of foveation. Astra verified the null-terminated chain is preserved. |
| Keep the bounded seam and defer claims requiring rendering. | **Retained.** Startup explicitly reports that no session/swapchain is attached. Renderer-owned images, actual task-enabled opaque multiview, moving-brush lighting, independently posed avatar instances and once-per-frame preparation remain P1 gates. |

The [Khronos enable2 specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc)
was rechecked for runtime-directed Vulkan creation and major/minor version
requirements. Its maximum describes the highest tested version; the backend
warns above it rather than rejecting a compatible newer API or a patch release.
The bootstrap selector still prefers the tested range when compatible.
[Vulkan multiview features](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMultiviewFeatures.html)
and [properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMultiviewProperties.html)
guide negotiation; enabling a feature bit alone does not implement stereo.

## Verification and limits

- Full native Linux Meson `debugoptimized` build with SDL3 passed, including all
  donor shaders and the C/C++ executable. The final cleanup correction was
  rebuilt and linked successfully. This is a diagnostic host build, not a
  release/GLIBC-ceiling qualification.
- The reused Vulkan boundary fixture passed version/provenance, dual creation
  results, independent/array eye ownership, incomplete-frame suppression and
  retirement-order cases. Its event-poll stub was updated to match the current
  backend. It does not exercise actual renderer/device creation or Windows
  error handling.
- `git diff --check` passed. The product checkout remains clean on `master`;
  local commits are confined to `2.0`. Nothing was pushed, installed or deployed.
- Native Windows/ARM builds and injected platform failure cases remain pending.
  The Windows cleanup correction has source-review evidence, not a native
  executed failure test. Worker-thread and future live-session fatal retirement
  must be resolved at session attachment; no live-session cleanup claim is made.
- No headset, performance or single-pass rendering result is claimed. The next
  implementation step is renderer-owned XR image attachment/submission, followed
  by the already required early multiview/lifetime proof before bulk ports.
