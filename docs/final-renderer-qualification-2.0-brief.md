# Verified final renderer qualification reopening

2026-10-01. Mostly-worked main brief for local Astra xhigh. Solo developer;
preserve vkQuake/OpenXR resource owners and desktop graphics. No renderer
rewrite, new lifetime coordinator or relaxed validation. Goal remains incomplete.

## Evidence and environment

| Fact | Status / reproducible source |
| --- | --- |
| Current complete Linux host build | [verified] Meson debugoptimized, GCC16.2.1, SDL3.4.16, codecs/CURL enabled, Steam Audio disabled preliminary configuration. Full build/rebuild succeeds through a1df3ffd. |
| GPU/driver | [verified] NVIDIA RTX4090, Vulkan1.4.351, driver615.71.09; AMD RADV integrated GPU also enumerated. No user headset testing. |
| Runtime | [verified] Installed Monado25.1 isolated simulated service; private runtime/config/data, forced XCB. Production default runtime not changed. |
| Validation | [verified] First Ubuntu layer1.3.275 was stale and produced unknown-pNext noise. Replaced privately with Arch Vulkan validation1.4.357. Do not use old-layer noise as engine evidence. |
| Signon0 stall | [verified/fixed] Actual complete engine consumed pext but console-only registration rejected src_client. Copied pinned QSS-M ClientCommand registration in a1df3ffd; subsequent actual desktop signon4 and12-frame checkpoint reached. |
| Current desktop synchronization failure | [verified] Current validation2 reports repeated queue-submit WRITE_AFTER_WRITE hazards at vkCmdEndRenderPass: prior subpass1 attachment store versus next render-pass final layout transition, COLOR_ATTACHMENT_OUTPUT writes. Actual stock start map, ordinary desktop, two alternating native primary command buffers. |
| Teardown abort | [verified] Both GDB checkpoint-quit and normal +exec cfg(map start,120 native waits,quit) abort with libc double-free/corruption, exit134 normal run.40-frame debugger stack ends in libnvidia-glcore/libGLX_nvidia destruction, with no retained engine frames. This does not establish which owner corrupted memory or that the driver is at fault. |
| Native baseline comparison | [unknown] Original vkQuake same-driver rendered/quit result not yet run. Do not call this port-only or dismiss it as upstream before comparison. |
| Relationship between hazards and heap failure | [unknown] May be separate; merge only with evidence. Neither is a measured performance gate. |
| Simulated XR matrix | [running] Real-engine existing24-probe GPU smoke relaunched after negotiation repair with current layer/nojoy. No completed result yet. |

Private local game logs (not Codex telemetry):
`/tmp/qsvr-final-qualification-thchgzi8/gpu/logs/desktop-smoke-repaired.log`,
`desktop-normal-exit.log`, `desktop-teardown-stack40.log`,
`xr-gpu-smoke-repaired.log`. Disposable readonly pak0 symlink, no mod/game edits.

## Source checks and options

[verified: main read actual source] r_passes.c:534–576 constructs incoming
external dependencies for each subpass and all earlier internal subpasses;
source stages ALL_GRAPHICS/COMPUTE/TRANSFER, source access MEMORY_WRITE,
destination graphics/rate-map access, internal by-region. No explicitly generated
outgoing dependency in that loop. Dependencies array bound is currently
N*(N+1)/2 at both builder and RenderPass2 conversion. Native warp pass separately
has explicit outgoing transfer dependency (r_passes.c:1519).

[verified: main read actual source] VID_Shutdown gl_vidsdl.c:6020 joins the native
end task, waits device idle under queue mutex, destroys VRIK state/pipelines/cache/
stereo UI layouts, retires XR when bound, clears tracking/rate state, then destroys
SDL window/video. Ordinary final path does not vkDestroyDevice/vkDestroyInstance;
current vkDestroyDevice use is only bootstrap failure. Host_Shutdown retains
native networking/audio/input/video order and does not call broad render-resource
destruction. Read baseline actual VID_Shutdown before inferring replacement need.

Main lean: correct demonstrated synchronization at existing generated-pass/
queue boundary; consider complete explicit outgoing store dependency with exact
array sizing versus native per-frame resource/barrier reuse. Do not assume an
incoming-only dependency proves store/final-layout coverage or use queue-idle
every frame as production policy. Verify attachment/view/image identities and
native outgoing/final transition scopes first.

For shutdown, compare baseline and current explicit destruction/SDL Vulkan-loader
ownership. Prefer narrow reuse of existing native join/drain/resource teardown
only if incompatible ownership is demonstrated. Adding unconditional destruction
of every object risks double destruction and runtime borrowed-image lifetime.
No speculative device rebuilder, per-frame allocation or retained leaked-driver
reference is approved. Unknown crash attribution is not replacement evidence.

## Requested review contract

Verify facts against current source/reference/logs before critique. Read-only:
Quake/r_passes.c, gl_vidsdl.c, gl_rmisc.c, host.c and their existing resource/
task/pipeline shutdown callees; compare readonly sibling vkquake at native base.
No production/docs/test edits, no builds/GPU runs, no branch switch/check, no
web agents or subdelegation. Main owns executable comparison and final fixes.

Return prioritized recommendations with source/file-line evidence, which
diagnostic/comparison must precede edits, smallest adapter design/write set/
estimate, expected correct rendered/exit behavior, risks and acceptance cases.
Challenge whether broad cleanup is necessary; identify deletion/reuse options.
Rank the issues and give the deepest bounded spec to the highest-value decision.
Max1000 words. Label uncertain inference and separate human decisions (none
currently established). Not final goal signoff: all other checklist groups open.
