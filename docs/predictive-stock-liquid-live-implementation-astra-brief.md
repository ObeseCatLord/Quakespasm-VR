# Stock live liquid replay implementation review

Archived initial review brief. Its immediate-snapshot pause evidence predates
the ordering issue found during this review. The later
[causal correction brief](predictive-pause-ordering-implementation-astra-brief.md)
and [final disposition](predictive-pause-ordering-2.0-plan.md#final-astra-review-and-disposition)
describe the integrated parser/producer changes and stronger acceptance.

Solo fork, bounded experimental stage of the committed
[liquid plan](predictive-stock-liquid-2.0-plan.md). Baseline `dc003e3a`;
default selected activation remains off. This review is not parent-goal or
arbitrary-mod completion. Preserve existing queue/QC/PMove/completion/snapshot
owners. Main branch and user-dirty `docs/migration-2.0.md` are outside writes.

## Checkable evidence and environment

| Fact | Status / evidence |
| --- | --- |
| Server gate admits alive stock WALK/SLIDEBOX at finite depth 0..3 with finite deadline/flags and timer 0..2; excludes pusher/pause/recovery/q30 | [verified source] Current `Quake/sv_main.c` diff. Positive timer requires FL_WATERJUMP; zero timer requires no flag or future external deadline. Existing selected authority/profile unchanged. |
| Both aggregate private engine-compatible contact rejection blocks removed | [verified source] Current `Quake/cl_main.c` diff. Shared solver fluid latch remains; public propagation untouched. No capability or persistent policy added. |
| PreThink/Think/contact/semantic timer corrections already reviewed/committed | [verified] `87c2768b`, previous ownership brief/disposition. New contract cases finish quiet/command deadline-only, flag-only and velocity-only composition. Actual pinned Think executes before prepared field output; not ordinary stock-writer reachability. |
| Actual live pending replay and positive-duration disposable preview now run | [verified normal software runs] `tests/stock_liquid_native_fixture.c`: water/slime/lava depth/swim, generated VR, e1m2 ledge. Existing wire-derived flat-swim bounds unchanged. Ledge differences logged separately. Preview proves journal/ACK/timer/netstate unchanged; desktop prepared pending axis, not physical XR preview. |
| Actual BSP surface entry/exit and three-world-frame delayed snapshots pass | [verified normal software matrix] New `tests/stock_liquid_transit_fixture.c`, water/slime/lava, generated VR, 10/25/100-ms commands. Real command/QC/world/snapshot/live replay. Bounds retain wire-derived error; no contents stubs or permission/ACK/selection staging. |
| Multiple commands queued before one world frame pass | [verified] Same matrix, `RunQueuedCrossing`: three commands at 10/25 ms or two at 100 ms; prepared longer outer frame feeds existing credit, actual per-command QC/solver/completion. Full snapshot/preview follow live pending comparison. |
| Wet pause/arrival gap and active ledge recovery pass | [verified] Same driver with e1m2 `-ledge-recovery`: real host pause/svc_setpause; replay denied while paused; fresh recovery fence/marker/completion reopens live replay and preserves owned T. Existing client stale-generation guard deliberately retains cached permission while paused, but actual replay checks cl.paused. Resume metadata changes epoch. No production parser change. |
| Actual QC drowning reaches death; selected/native event tuples agree | [verified independent runs] `-drown-only` selected and native from separate initialized stock worlds with expired air/pain clocks prepared. Ten actual frame/time/health/depth events agree; health never forced. Terminal full snapshot withholds live replay. |
| Future/NaN deadline, mismatched timer/flag and pusher marker deny permission | [verified predicate-only cases] Prepared field inputs, real complete snapshots in transit driver. Not pusher geometry qualification. Existing actual teleport contracts still apply. |
| No real-BSP one-command dry→wet→dry proof yet | [explicit unknown] `tests/pmove_migration_fixture.c` has actual solver transient-contact test using synthetic hull/contents seam. Entry/exit and growing delayed history are actual BSP; do not equate them with every path. |
| Local Linux SDL3 -Werror available; sockets/ptrace unavailable | [verified environment] Captured transport and prepared client signon/resources/input are explicit component seams. No connected/XR/headset performance claims. Windows/ARM and user's live trials deferred. |
| Final relevant software checks pass | [verified] Linux build, updated client replay ASan/UBSan, partial combined transit ASan/UBSan including drowning and e1m2 ledge recovery, mixed early-pause/arrival-gap/native/loss/wrap/death/respawn acceptance. Combined fixture instruments named included sources, not whole engine. |

## Decisions and minimum architecture

Lean: retain this coherent experimental release via the existing permission,
with qualification limits stated explicitly. Reject another liquid capability,
timer owner, stored validity bit or duplicated replay state machine: prior
ownership correction makes the existing server seeds sufficient. Challenge this
lean if the actual client/server path contradicts it; a missing test is not by
itself evidence for replacement. If a released domain cannot be qualified,
prefer a narrow existing-boundary restriction over a broad rewrite.

Main will run final relevant regression/sanitizer checks and update docs while
production stays frozen for this review. Verify-first read-only scope: current
`sv_main.c`/`cl_main.c` diff, existing selected timer/completion and stat owners,
the three stock liquid drivers and replay fixture, linked plan as needed.
Do not re-review all migration architecture, invent transport, run tests/edit
files/spawn agents, or export operational telemetry. Verify effective Astra/max
using only turn_context model/effort fields.

Return <=750 words: scope_done/effective model-effort; ranked blockers versus
bounded unknowns with file/line evidence; minimum fixes/acceptance; whether this
existing-owner architecture is sufficient. Rank the actual risks yourself,
including pause-generation interaction and pending history/preview timer seeds.
