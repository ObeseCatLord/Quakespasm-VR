# Local multiview GPU qualification and review

Reviewed 2026-09-20 by Astra (`gpt-6-astra`, explicitly `xhigh`), with effective
settings verified by main through a narrow read-only metadata query. The review
was read-only. A Terra worker investigated the acceleration-structure dependency;
main reviewed its patch, used the GPU counterexample to correct the remaining
dependencies, and integrated the resize fix and test harness.

## Decision and evidence

Preserve donor rendering, resource allocation, task scheduling and queue ownership.
Two demonstrated incompatibilities require small corrections at their owners:

- A queued SDL desktop-size event overwrote the attached OpenXR target extent.
  Live inspection showed a 640x480 render extent against 896x1007 runtime images;
  the compositor showed scene output confined to part of each eye. SDL2 and SDL3
  now call `VID_WindowSizeChanged`, which preserves the runtime extent while
  updating the already-existing remembered desktop size. Ordinary desktop events
  retain their previous resize/restart/console behavior.
- Synchronization validation reported a read-after-write hazard between TLAS
  scratch writes and a later animated BLAS build. The existing allocation also
  stores compute-generated vertices. The compute-to-build barrier must name
  `SHADER_READ` for geometry input; the terminal TLAS barrier must cover the
  subsequent compute and AS uses of the allocation. An initial patch adding
  only AS-write access did not resolve the live hazard and was corrected.

A second allocator, frame graph or resize state machine would duplicate existing
owners without addressing a separate demonstrated requirement. The fixes add no
per-frame device-idle wait and do not disable ray queries.

## Senior review disposition

| Finding or recommendation | Disposition |
|---|---|
| The scratch allocation also holds geometry inputs, whose access class differs from AS scratch. | **Adopted.** Main verified the allocation and Vulkan contracts. `gl_mesh.c` now publishes compute writes to shader reads at the AS-build stage; `r_brush.c` covers subsequent AS and shader reads/writes after TLAS construction. The existing conservative destination stage remains. |
| `maxVertex` is a highest vertex index, not a vertex count. | **Adopted.** Both animated BLAS size-query and build descriptions now use `numverts_vbo - 1`; allocation rejects nonpositive vertex counts first. This adjacent bounds defect is distinct from the observed synchronization hazard. |
| A requested cvar matrix is not proof that effective rendering modes changed. | **Adopted.** The GDB smoke gate reads effective OIT, sample count and indirect state after settling each combination, checks task rendering, and captures initial and post-resize compositor output. It uses existing command/function boundaries rather than a production test protocol. |
| Preserve the small SDL boundary correction. | **Adopted.** One helper and existing remembered dimensions serve both SDL versions. A production-code fixture reproduces delayed startup events and ordinary desktop resizing. |
| Do not close the full P1 gate from a stock-map renderer smoke run. | **Adopted.** Independent live avatar/shadow poses, moving/scaled model and eye-only visibility cases, inherited weapon/movement behavior, headset acceptance and performance remain open. |

## Evidence quality and remaining limits

The local environment uses an isolated Monado simulated service, its matching
OpenXR runtime, the host NVIDIA RTX 4090, and Khronos synchronization validation.
The service uses private runtime/config directories. No headset, gaze provider,
user runtime selection or deployed game configuration is required or changed.
See [reproduction instructions](../tests/README.md#local-openxr-gpu-smoke).

Earlier startup-command tests were rejected as scene evidence: map-loading
keepalive refreshes consumed their waits before client signon. Their identical
loading-screen eye captures do not qualify live stereo. The replacement inserts
the normal command script only after signon and an active XR frame. Optimized
GDB source breakpoints can resolve to multiple locations; diagnostic command and
quit probes therefore use a single function-entry address.

The resulting checks cover a stock `start` scene, simulated head motion, array
palette processing, transparency and MSAA resource transitions, indirect on/off,
pause, desktop-window restart while XR is attached, and normal teardown.
Synchronization validation and visibly different full-size eye images are useful
renderer evidence, not proof of geometric parity, correct VR HUD placement,
controller behavior, all model formats, headset comfort or faster frame times.
Windows and ARM qualification remain deferred until the end, as requested.

Astra's focused follow-up independently checked all 13 effective-mode probes,
frame progress from 76 to 1,136, final dimensions, normal exit, absence of
validation/harness errors and both compositor captures. It closed the resize,
synchronization, geometry-bound and harness findings with no remaining blocker
in this narrow scope. The Linux build and production resize fixture also passed.

Official contracts used in the review:

- [Vulkan acceleration-structure build synchronization](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html): scratch uses AS read/write access; geometry inputs use shader-read access at the AS-build stage.
- [Triangle geometry bounds](https://docs.vulkan.org/refpages/latest/refpages/source/VkAccelerationStructureGeometryTrianglesDataKHR.html): `maxVertex` is the number of vertices minus one.
- [Monado development documentation](https://monado.freedesktop.org/developing-with-monado.html): simulated devices support local development without an HMD.
