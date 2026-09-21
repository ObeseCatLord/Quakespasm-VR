# Initial multiview integration: senior review and disposition

Reviewed 2026-09-20 by Astra (`gpt-6-astra`, `xhigh`). Main verified the effective
model/effort through a narrow read-only metadata query. The reviewer did not
have that metadata itself; its review report accurately left self-certification
open. One Terra worker implemented shader/pipeline changes; main integrated,
reviewed, fixed lifecycle/visibility issues and performed local checks.

## Decision and verified brief

The chosen design adapts donor resources, render passes and draw paths rather
than replacing the renderer. Evidence: `gl_vidsdl.c` owns runtime image views and
the existing Vulkan submission, `r_passes.c` adds two-view masks to the existing
pass descriptions, `gl_rmisc.c` selects stereo variants of existing scene vertex
shaders, and `gl_screen.c` retains the task graph with one added uniform-publication
dependency. The backend session state machine is reused.

Relative eye clip transforms preserve existing per-model MVPs. A second complete
engine render would duplicate mutable work; a new frame graph or submission
service would duplicate working ownership. Neither is justified by the observed
incompatibilities. Extra vertex math and conservative visibility are unmeasured
costs, not evidence of a speedup. First-head anchoring and fixed default scale
remain temporary renderer-proof policy to replace with inherited gameplay code.

Before review, main verified the call graph, array resources, frame submission
join, queue lock boundary, shader generation and culling changes. Build/fixtures
were initially unverified, then passed during review. Actual GPU multiview,
headset behavior and performance remained explicitly unknown. The read-only
review focused on lifetime/abort/restart, Vulkan multiview correctness and camera/
visibility; it excluded netcode, full historical scope and Windows/ARM delivery.

## Disposition

| Recommendation or finding | Main disposition and evidence |
|---|---|
| An abandoned begin rotates dynamic buffers without advancing the command-buffer slot; the following begin can overwrite a previous submission's storage. | **Adopted.** Verified `R_SwapDynamicBuffers` advances during begin while `current_cb_index` advances after submission. `SCR_AbortXRFrame` now uses the existing `GL_WaitForDeviceIdle` before backend abort and camera restoration. This exceptional drain also protects begin-driven garbage collection. No new frame clock or allocator is introduced. |
| A reference-space notification on a skipped frame disappears before camera preparation. | **Adopted.** `GL_BeginRendering` invalidates the existing camera anchor immediately after backend begin, before early returns; the next valid `R_PrepareStereoFrame` establishes it. Retirement invalidates the anchor too. |
| Require real per-eye scene output, not just pass-creation spies and numeric matrices. | **Adopted as an open acceptance gate.** Desktop `start` rendered locally, but no multiview GPU or headset result is claimed. P1 stays open. The required scene includes world plus a moving/scaled model, eye-only visibility, indirect on/off, transparency/MSAA and one array color effect, with pause/recenter/restart/abort cases. |
| Keep the incremental adapter; neither finding requires a replacement renderer or submission service. | **Adopted.** Retain vkQuake task, resource, model-transform, VM and simulation owners; replace temporary camera policy with inherited gameplay code in subsequent slices. |
| An already-recording command buffer cannot be begun again. | **Withdrawn by reviewer after specification verification.** No unnecessary reset layer was added. This does not remove the independent dynamic-storage lifetime issue. |
| Treat initial headset anchoring/default scale as temporary rather than a second gameplay implementation. | **Adopted.** Explicitly recorded in status and acceptance limitations. |

Main integration review separately corrected a color-effects format mismatch
(compute writes donor internal color, not the XR output format), excluded raster
texture warping from stereo vertex selection, preserved caller shader modules
across pipeline creation, and gated multiview shader modules on enabled support.
The XR SRGB output now preserves donor gamma through an exact inverse transfer.
Desktop window changes retain eye-target dimensions.

Local GPU smoke testing exposed a shutdown race in the inherited asynchronous
screenshot path. Main verified the core showed `WriteScreenshot` freeing a Vulkan
buffer concurrently with driver/window teardown. `VID_Shutdown` now joins the
existing rendering task and drains the queue under its existing mutex before
SDL teardown, including desktop mode. The same screenshot-and-quit test then
exited 0. No new teardown service or screenshot worker was introduced.

Astra performed a focused follow-up inspection and closed both findings; it
also confirmed that shutdown joins the screenshot task before locking the queue,
so the fix does not introduce a worker/mutex deadlock. The screenshot rerun was
completed by main afterward, with exit status 0.

## Evidence and limits

The Linux build and tests are described in [tests/README.md](../tests/README.md).
The camera fixture invokes production preparation/restoration and abort code,
but spies the GPU drain. The numeric fixture independently projects points;
it does not prove full model/PVS/GPU composition. Pass creation/recording checks
use Vulkan spies. None replaces validation-clean, visibly correct stereo images.
The real desktop screenshot verifies stock scene rendering and corrected quit
ordering on this host, not comprehensive desktop parity.

Official contracts consulted:

- [Vulkan multiview render-pass contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassMultiviewCreateInfo.html): view masks and per-view attachment access, including input attachments.
- [Shader-module requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkShaderModuleCreateInfo.html): shader capabilities require device support.
- [Command-buffer lifecycle](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#commandbuffers-lifecycle): reviewer corrected the command-buffer reset objection against this reference.
- [Device idle wait](https://docs.vulkan.org/refpages/latest/refpages/source/vkDeviceWaitIdle.html): exceptional GPU retirement uses the existing device/queue synchronization boundary.

This review earned its cost by finding the allocator-retirement and skipped-frame
reference defects. It is not a completed gameplay or performance certification.
