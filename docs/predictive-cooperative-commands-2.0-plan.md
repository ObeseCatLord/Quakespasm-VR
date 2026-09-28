# Cooperative QC on accepted command time

Status: verified stage2 plan before production edits, following `a321e45b`.
Main owns integration on `2.0`; the user's migration document is untouched.
This extends, rather than replaces, the full movement and migration goals.

## Behavior and evidence

An admitted private cooperative player executes PreThink, the movement hook and
PostThink once for each executable accepted command, with that command's input
and duration. Scheduled Think retains one opportunity per world frame. A queued
suffix without credit receives no callbacks, roomscale or completion. Public
desktop players keep the native-world hook from stage1. Customphysics and
terminal/held states retain the current native authority boundary until their
command contract is qualified. Client replay of an arbitrary QC transform is
not equivalent to plain PMove; this stage must not advertise that equivalence.

| Fact | Source verification |
| --- | --- |
| Accepted queue, finite capped credit, bounded8-head loop and completion/debit already exist. | `sv_phys.c:SV_Physics_ClientPrivateWalkTrial`, `SV_Physics_Client`; retirement is `sv_user.c:SV_FinishPrivateUsercmds`. |
| Cooperative hooks currently classify native; native selection coalesces all queued inputs into one world callback. | `sv_phys.c:SV_PrivateWalkTrialClassifyOwner`, `SV_Physics_ClientSelectedNativeFrame`. |
| Native phase helper already owns input snapshots, roomscale, lifetime, PreThink/customphysics/Think/hook/PostThink, contacts and completion. | `sv_phys.c:SV_Physics_ClientNativeFromPhase`. Its caller supplies host/QC duration. |
| Scheduled Think can use the world clock independently of command duration. | `sv_phys.c:SV_RunClientWeaponThink`, `sv_client_think_window_t`. |
| Publication grants solver replay only to classified WALK engine owners. | `sv_main.c:SVFTE_WriteEntitiesToClient`; cooperative owners currently remain native authority. |
| Donor cooperative callbacks are per input with input_timelength, while receipt execution is a different queue/cadence owner. | Read-only `QSS-M/Quake/sv_user.c:635–703`. Reuse callback semantics, not a second receipt dispatcher. |

Known limits: the standard adapter currently lacks Gorilla metadata and full
client-side cooperative replay. Initial selection is still dry/multislot and
excludes loadgame. These are mandatory later movement work, not reasons to
invent another protocol or whitelist. Arbitrary authored callback/replay
semantics remain unknown.

## Minimal reuse versus another movement implementation

Preferred: keep the existing selected loop and its credit/queue/completion
owner. Add a cooperative branch in `SV_Physics_ClientPrivateWalkTrial` before
the plain-WALK preflight. It delegates the actual lifecycle to
`SV_Physics_ClientNativeFromPhase` at accepted duration with the existing world
Think window. Frame validation remains observational. Keep classification and
legacy/correction publication truthful; no new state enum, wire authority,
solver, queue, scheduler, policy cvar or program identity is needed.

The alternative is a separate cooperative queue wrapper duplicating credit,
staging, maintenance and debit, or many hook-specific branches throughout the
plain-PMove body. Both duplicate working owners without a demonstrated need.
Copying QSS-M receipt execution duplicates the world queue as well. Reopen if
the preferred adapter needs a persistent per-program owner or a new replay lane.

Expected production scope: approximately100 lines in `Quake/sv_phys.c`, plus
bounded fixture/docs changes. Do not refactor adjacent stock/q30/shared owners.

## Exact contract and implementation order

1. Add a small observational cooperative-owner predicate: live validated native
   classification, actual command hook, no independent customphysics and no
   held-motion/terminal boundary. Use it only to choose the existing selected
   command path; ordinary native/public/customphysics flow remains the reference.
2. In the existing selected command function, use frame validation for this
   branch and skip direct WALK solver preflight. Reuse its existing credit and
   queue-head selection. Stage this head's levels/impulse/angles, supply its
   accepted duration, then call the native lifecycle helper with the shared
   world Think window. Its fresh phase owns roomscale exactly once.
3. Quiet maintenance uses only last completed levels/pose, zero impulse and
   roomscale, zero input/host/QC duration and the completed cursor. Run zero-time
   PreThink/PostThink and world scheduled Think, but skip the command hook and
   ordinary body movement. A hook's duration-independent side effects belong to
   an accepted command, never a synthetic repeated completed cursor.
4. Reuse the existing common completion/debit tail. Positive completion requires
   the real PostThink/lifetime tail. Each cooperative head owns its own QC
   lifecycle, so impulses clear per head. The native helper reports a borrowed
   per-call sticky boundary outcome: observe eligibility after callback phases
   and latch actual customphysics execution. Later PostThink restoration cannot
   erase an earlier handoff. A callback changing ownership/death
   finishes this head, fences the unstarted suffix and leaves the next frame to
   the existing native dispatcher. Do not zero accumulated credit after every
   ordinary cooperative head or break batching merely because its snapshot
   authority remains native. Cooperative execution uses the existing8-head loop
   without unaware-QC button/impulse prefix restrictions; preserve the accepted
   sequence upper-bound check independently of shared-QC mode. Existing
   frame-end retirement stays unchanged.
5. Keep solver replay permission denied for arbitrary cooperative QC. The
   prepared fixture deliberately halves wish speed, which a plain client solver
   cannot reproduce. Completing per-command execution is not full cooperative
   prediction or full movement parity.

Production ownership: only `Quake/sv_phys.c`, including one optional borrowed
boundary-outcome argument at the existing native helper and its callsites.
Test/docs ownership:
`tests/cooperative_qc_native_fixture.c`, its generator only if required for
callback evidence, `tests/README.md`, this plan and the plan index. Main performs
edits while the requested Luna coding route is unavailable; local Astra is
read-only design review. No concurrent writer owns these paths.

## Acceptance and deferred obligations

Extend the actual loader/VM/public-private/full-parser fixture with selected
command cases: distinct accepted durations, two heads with a button/impulse
transition, insufficient-credit retained suffix, no-command zero-time
maintenance with a duration-independent hook counter, once-world scheduled
Think including later-head scheduling, exact completed/retired cursors and
single roomscale per head. Prove actual transformed body displacement and
negative replay permission, not just hook counters. Existing native0/1/2-call
and stock/q30/older-AD/customphysics checks run after coherent implementation.
Add a prepared customphysics handoff restored by PostThink to prove sticky
suffix fencing. Document prepared QC/starts/captured transport and any direct
diagnostic seams.

Gorilla/instant-stop sample ownership, wet/ladder/custom/terminal transitions,
local/load admission and compatible cooperative replay remain later required
stages in the parent plan. No device/performance/Windows/ARM test is required
for this implementation pass.

## Astra review questions

Challenge whether native lifecycle reuse is truly sufficient, especially Think
duration, completed-cursor ownership and customphysics/terminal transitions.
Prefer simplification/deletion over a new cooperative owner. Decide whether
zero-time maintenance should invoke the hook at all, and which authored writes
need an explicit command-time boundary. The current lean is zero-time hook
execution with truthful native authority. Identify any coupled prerequisite
that makes this stage misleading or wrong before production changes.

## Astra design disposition

Local Astra Max verified the source before reviewing. Effective
`gpt-6-astra`/`max` settings were checked from local metadata. Main spot-checked
the native helper's customphysics/PostThink/completion tail, the prefix/sequence
guard and the donor received-command hook. Review was read-only, without tests.

| Finding/recommendation | Disposition |
| --- | --- |
| Rechecking eligibility only after the whole helper misses an intermediate handoff later restored by QC. | Adopted: one optional borrowed sticky outcome, observed after relevant callback phases; actual customphysics execution latches it. Finish/debit the current head once and defer the untouched suffix. No persistent state or new dispatcher. |
| The unaware-QC prefix would stop at later buttons/impulses; disabling shared mode also loses the sequence upper bound. | Adopted: cooperative execution keeps the existing8-head loop/per-head credit and validates sequence<=lastmovemessage explicitly. Preserve unaware-QC restrictions for unaware programs. |
| A zero-time movement hook can repeat duration-independent gameplay side effects. | Adopted: skip the hook in quiet maintenance while retaining zero-time Pre/Post, world Think and no ordinary movement. The prepared QC counter is already duration-independent. |
| The shared Think opportunity is consumed even when nothing is due. | Adopted/preserved: hook/PostThink or later-head scheduling waits until the next world frame. Add explicit software evidence for that ordering. |
| Native classification and correction-only publication can remain unchanged. | Adopted: arbitrary cooperative QC is not plain solver replay. Full compatible prediction remains parent scope. |

These source-proven corrections revise the contract before production edits;
no human preference or approval decision is needed.
