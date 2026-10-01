# Shutdown through the existing render-resource owner

2026-10-01. Final desktop normal-quit defect; both untouched vkQuake and2.0
abort during driver-library process finalization. Root-cause identity/driver
fault is not claimed. No new shutdown/device state machine is justified.

Main read VID_Shutdown and GL_DestroyRenderResources. VID_Shutdown already waits
for rendering/device work but destroys only pipelines/cache/layouts before SDL
window/video shutdown. The existing restart owner GL_DestroyRenderResources
already retires complete framebuffer/attachment/descriptor/swapchain/AO/mirror/
XR-view/render-pass resources in native dependency order and waits for idle.
The normal quit path leaves those objects alive. A private GDB diagnostic called
that existing owner at VID_Shutdown entry: actual desktop signon4/twelve frames,
no validation errors/hazards, ordinary inferior exit0. The binary included the
temporary synchronization barrier, so independent final-tree proof remains required.
Private evidence: desktop-teardown-resource-owner-probe.log. No source patch was
used for that diagnostic. This establishes the existing owner suffices in the
tested case, not precise allocator-corruption internals.

Smallest correction in Quake/gl_vidsdl.c only: replace the partial resource branch
with GL_DestroyRenderResources when render_resources_created is true, otherwise
retain R_VRIKRenderShutdown. Keep the existing end-task/device wait, cache/stereo
layout retirement, XR shutdown/forget, rate-state clear, cursors/window/video and
platform teardown. Retire app-owned framebuffer/views before runtime swapchains.
Do not add a Vulkan device/instance teardown framework, duplicate destruction,
skip SDL cleanup or exit early to conceal a fault. Native resource owners remain
authoritative, as required by the postmortem/reuse discipline.

Main owns this narrow change, approximately12 changed lines, reopen before25.
Luna independently owns r_passes.c; another worker owns metadata tests. Main
reviews complete patch and integrates with the pass correction before one full
host rebuild and affected desktop/actual simulated-XR runs. Actual default native
process exit0 and clean validation are required; GDB injection alone is not final
acceptance. Include restart/resize and XR shutdown lifetimes in affected coverage.
No hardware/performance/Windows gate or new feature is added.
