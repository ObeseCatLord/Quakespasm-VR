# Stock wet permission: completion and quiet-frame ownership

Solo fork, bounded continuation of
[the existing liquid plan](predictive-stock-liquid-2.0-plan.md). Baseline
`14dd5120`; plans and two prior fresh Astra Max dispositions precede this
permission stage. Keep the full migration/default/mod outcome intact.

## Verified evidence and unknowns

| Fact | Verification |
| --- | --- |
| `2.0` worktree, only user-dirty `docs/migration-2.0.md` at start | Current status; user document outside writes. Main branch untouched. |
| Actual admitted stock `e1m1` depth/jump/swim runs and `e1m2` ledge start/end succeed with pending diagnostic solver comparisons | Previous normal seven-run matrix and partial ASan/UBSan fixture pass. Transport captured, client/input/resource state prepared. Shadow bypasses policy; no live wet claim. |
| Stock swim overwrite correction is implemented without a blanket velocity restore | `sv_phys.c` water adapter, scheduled Think unchanged-velocity boundary. Prior Astra identified later recategorization/flag eligibility as unqualified. |
| Server permission rejects all wet/active-waterjump states plus future `teleport_time`; client history and preview reject all fluid contact | `sv_main.c:SVFTE_WriteEntitiesToClient`, both `cl_main.c` gates. Existing authority/permission sufficient for lockstep private release. |
| Selected solver captures timer, writes FL_WATERJUMP/deadline before impacts/triggers/PostThink, then commits the captured timer afterward | `sv_phys.c` PMove result/edict write/completion tail. Callback deadline/flag change can conflict with that seed. |
| Stock QC has CheckWaterJump deadline/impulse and actual teleport_touch writes a .7-second hold, fixangle and authored velocity | Local stock reference plus prior actual pinned QC decode. `world.c:SV_RecordRecentTeleportTrigger` recognizes semantic triggers and calls existing `SV_PrivatePlayerTeleported`. Do not assume generic setorigin is a teleport. |
| Existing semantic relocation owner increments private discontinuity epoch and invalidates hands; it does not currently cancel private jump timers | `sv_user.c:SV_PrivatePlayerTeleported`; explicit host relocation and cooperative/world teleport callers. |
| Quiet selected maintenance runs zero-duration PreThink/Think/link/PostThink without reconciling stock wet movement writes | `sv_phys.c:SV_Physics_ClientPrivateWalkTrial` !run_command branch. Actual quiet-ledged mismatch unknown pending main probe; source shows a possible stock 225 refresh over the solver's 310 and retained command timer. |
| Pusher interaction is already one frame-local replay exclusion | `server.h`, `sv_phys.c`, `sv_main.c`; preserve rather than simulate pushers per command. |
| Hazardous real-map damage/transit and callback/hold matrix remain unknown | Main extends the existing captured real-map fixture. No fake contents or new production test transport. |
| Linux available; sockets/ptrace unavailable; Windows/ARM, live device/eye and benchmarks deferred | Prior environment failures/user scope; do not broaden review into those features. |

## Proposed minimum and forks

Retain all queue/credit/QC/PMove/ACK/snapshot owners. Capture the stock PreThink
movement handoff inputs/outputs **before** later validation recategorizes water
or scheduled Think changes eligibility. Reconcile the known QC writes from
that witness, keeping scheduled Think velocity precedence and authored residuals.
Reuse the same zero-duration adapter in quiet maintenance, rather than letting
its stock jump/swim/ledge writes acquire a new movement owner. Do not execute a
synthetic PMove command or advance ACK/timers merely to make maintenance easier.

At the existing completion boundary, compare the actual final waterjump
flag/deadline and semantic-relocation epoch with the just-written solver state.
Lean: a callback taking deadline/relocation ownership cancels that solver jump,
preserves its actual authored deadline/origin/velocity, and exports a coherent
zero private jump timer. Its real teleporter hold continues to suppress replay;
subsequent command seed/backmove/deadline clearing must use the private owned
timer rather than a stale QC FL_WATERJUMP bit. Use the existing semantic
relocation owner where appropriate and guard against the completion tail
restoring a pre-callback timer. Exact flag treatment needs your challenge.

An alternative is one completion-validity Boolean, consumed by the existing
permission predicate, rather than canceling conflicting timer state. Its cost:
every quiet/callback boundary must maintain it, and it can hide a stale solver
seed even while permission is off. Lean against it unless it demonstrably
removes complexity. Another option is broaden the existing frame-local pusher
exclusion to world/callback interaction; that still cannot by itself repair
timer ownership. No extra persistent deadline, timer journal, capability bit,
native fallback after partial QC, wet state machine or transport is justified.

After the full released fluid domain is qualified, allow stock wet replay
through existing permission and remove both aggregate client fluid restrictions
coherently. Active waterjump may bypass the deadline hold only when its completed
flag/timer/deadline remain solver-owned. Callback/teleport, terminal/native,
pause/recovery/pusher and unsupported-QC restrictions remain. Public PREDINFO
continues unchanged. If a released type cannot be qualified, retain explicit
substep contact rejection for that type; final water type cannot prove crossings.

Main is independently extending real-map fluid and quiet-frame probes while
you review; those current results are unknown. Review read-only exact scope:
this brief/plan, named server completion/maintenance/semantic teleport owners,
private stat/permission and replay timer seeds; bounded stock/QSS-M reference
only as needed. No edits, agents or test reruns. Verify effective Astra/max using
only turn_context model/effort fields. Return <=850 words: verified findings,
ranked minimum contract/disposition, exact flag/timer/deadline ownership rules,
necessary proof. Challenge architecture and seek deletion; report missing
evidence without widening into a rewrite or unrelated acceptance inventory.
