# Substantive upstream merge rehearsal: actual conflicts

2026-10-01. Disposable shared clone at production `a1df3ffd`; official upstream
pin `0d8121387e4c988951d2d58952793fa2482ef290`,36 commits after common baseline
`4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`. Executed `git merge --no-commit
--no-ff` against the exact pin. Expected conflict exit1: **14 files,35 conflict
blocks**, counted by exact Git markers (ordinary source separator comments were
excluded). Original production/main/history remain untouched.

Local evidence: `/tmp/qsvr-final-qualification-thchgzi8/upstream-rehearsal` and
`logs/upstream-merge.log` under its parent. Main inspected actual conflict blocks,
including surrounding owners. The disposable clone remains unresolved; no merged
build or behavioral acceptance is claimed. This is evidence of integration cost,
not a promise of conflict-free upstream updates or a request to expand features.

| Actual conflicted file | Blocks | Native change / port seam and proposed adapter owner |
| --- | ---: | --- |
| Quake/common.c |1| Upstream start-argument discovery collides with Steam/rerelease fallback locals. Preserve native start argument and narrow discovery fallback variables in COM_InitFilesystem; no search-path replacement. |
| Quake/gl_model.c |1| Native unnamed-texture fallback collides with bounded copied texture-name termination. Retain actual source/destination widths and explicit termination, then apply generic fallback at native texture load. Do not choose one memcpy blindly. |
| Quake/gl_rmisc.c |1| Native SSAO quality-specialized compute pipelines collide with stereo shader selection. Reuse pipeline creation/quality selection, adding the existing stereo module choice within that owner. |
| Quake/gl_screen.c |1| Native center-background helper/signature changed. Adapt the existing physical/desktop canvas call at that draw boundary; retain native content. |
| Quake/host.c |1| Native save-thread wait precedes VM destruction; port frees pending loaded-client records. Keep both at Host_ClearMemory in the correct resource lifetime order. |
| Quake/host_cmd.c |12| Native asynchronous save snapshots/writer, autosave registration and map filtering overlap inherited co-op v7/passive save records, atomic completion, loaded clients and catalogue filters. Reuse the native snapshot/writer owner; capture extra co-op records on the main thread before publication, preserving v5/v6/v7 distinctions, atomic completion/failure reporting and no worker QC access. This needs a bounded plan/review before actual integration; retain generic map filter and native new commands at their existing boundaries. |
| Quake/menu.c |2| Native preview/mouse-enable/high-DPI policy overlaps tracked pointer override and VR click helpers. Reuse native menu preview/control helpers, isolate tracked-coordinate substitution, and scope desktop mouse opt-out to its input owner. Keep both helper families; a textual ours/theirs choice loses behavior. |
| Quake/r_ssao.c |8| Native full/half-resolution working images/lookups and quality selection overlap per-eye images/descriptors, stereo array composite and VR resolution. Eye and resolution axes both have size2 but different semantics. Preserve native desktop AO at its owner and explicitly adapt independent eye/resolution indexing; avoid duplicated AO state machines. This is a coupled renderer adapter requiring its own bounded plan. |
| Quake/server.h |1| Native autosave state overlaps port co-op autosave state. Reconcile policy at the save owner before choosing fields; do not introduce two active schedulers for one save. |
| Quake/sv_main.c |2| Native gameplay-fix declarations and split-signon buffers collide with metadata-before2 drain, exact-fit/pressure admission and post-spawn userinfo obligations. Keep native split-buffer sender, with metadata admission at the same staged owner and unchanged local/remote chunk policy; preserve retries and visible permanent incompatibility. Requires coupled signon qualification, not a direct marker deletion. |
| Quake/sv_phys.c |1| New native client-movement locals collide with auxiliary private roomscale locals. Check semantic scope and shared movement-frame ownership; keep roomscale adaptation at the command/movement boundary rather than copying a second solver. |
| Quake/sys.h |1| Equivalent atomic rename/remove declarations differ in names/comments. Keep one native declaration per API; no new file abstraction. |
| Quake/sys_sdl_unix.c |1| Equivalent libc rename wrapper differs only in parameter names. Keep the native implementation. |
| Shaders/ssao_composite.inc |2| Native resolution-aware depth lookup/visibility upsample overlaps stereo layered fetch and VR bilateral composite. Reuse the native composite helper with the existing eye-aware fetch boundary; preserve desktop output and inspect thin-geometry stereo depth edges. Coupled with r_ssao/gl_rmisc. |

## Maintainability disposition

Most conflicts are bounded API/call-site adaptations. Four coupled regions carry
the real integration risk: save/autosave ownership, SSAO eye/resolution indexing,
staged signon/metadata admission, and shared predictive/private movement.
The rehearsal provides actual evidence for those risks; it does not justify
rewriting the native systems or automatically adding every newer upstream feature
to this migration's frozen scope.

The existing native architecture remains mergeable through explicit adapter
owners, but this unresolved rehearsal cannot prove a successful merged build.
Before a real upstream update, record bounded plans for the coupled regions,
resolve all conflicts, then build and qualify native desktop plus stereo/network/
save behavior. No production upstream merge is part of this receipt.
