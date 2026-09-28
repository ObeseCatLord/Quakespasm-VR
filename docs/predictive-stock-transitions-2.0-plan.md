# Selected stock movement: native state transitions

Status: first frame-boundary slice implemented with passing Linux/software
checks and local Astra Max implementation review. The plan preceded production
edits (`2011abaa`, reviewed revision `66da8b6b`). This is a
bounded implementation plan, not a declaration that prediction or the migration
is complete. The parent [predictive movement plan](predictive-movement-2.0-plan.md)
retains mod support and the full desktop/VR acceptance matrix.

## Outcome and references

An admitted stock-QC player can use vkQuake's `noclip` and `fly` commands and
return to WALK without disconnecting, replaying accepted head movement, losing
brief attack/impulse input, or running player callbacks twice. Native states use
vkQuake's existing frame movement; qualified WALK uses the existing selected
command owner. Desktop and VR share this policy. No new movement setting,
protocol, queue, solver or QuakeC dispatcher is needed.

References are the native `SV_ClientThink` plus `SV_Physics_ClientNativeFrame`
chain on this branch, inherited from vkQuake, and the existing selected death/
respawn continuation. The intended improvement is predictive WALK with native
behavior preserved where replay is not qualified. Native authority explicitly
withholds private prediction; returning to WALK must publish a fresh owner reset
and use the existing authority/mode epoch to invalidate incompatible replay.

## Verified facts and unknowns

| Evidence | Consequence |
| --- | --- |
| `Quake/host_cmd.c`, `Host_Noclip_f`/`Host_Fly_f`, change living owners to NOCLIP/FLY. | These are valid stock engine states. |
| `Quake/sv_user.c`, `SV_PrivateWalkTrialStateValid`, rejects anything outside WALK/SLIDEBOX. `Quake/sv_main.c`, admission and snapshot validation, do the same. | Ordinary cheats currently disconnect an already selected client. Changing only the physics dispatcher is insufficient. |
| `Quake/sv_phys.c`, `SV_Physics_ClientTerminalFrame`, coalesces queued command levels/latches before the existing native dispatcher. | Reuse that adapter, retaining dead-only contact invalidation and roomscale clearing. |
| `SV_RunClients` skips `SV_ClientThink` for every selected owner; `SV_ClientThink` applies native acceleration. | A living native frame needs exactly one native input pass before PreThink. |
| `SV_ClientUpdateAnglesForClient` decays punch angle; `SV_ClientThink` already calls it. | Replace the coalescer's angle-only call on the living branch, rather than adding native input after it. |
| `SV_GorillaEligible` and `SV_GorillaConsumeDeferredMove` reject all selected owners, including ones executing native physics. | Establish native execution before native input and adapt these exact gates; keep selected WALK on its solver owner. |
| `SV_Physics_ClientPrivateWalkTrial` already continues terminal transitions after PreThink/Think without restarting callbacks. Native continuation after weapon Think retains the pre-Think WALK dispatch type. | Do not generalize partial callback continuation by changing a boolean; phase/type/time ownership must be separately justified. |
| The selected solver uses command duration; native frames use world duration. | Switching to a fresh full native frame after one or more completed commands could double movement time. Select a fresh native frame before selected callbacks. |
| `SVFTE_WriteEntitiesToClient` already publishes native/engine authority and advances a mode epoch. `CL_ObservePrivateReplayMetadata` clears replay propagation on authority/epoch changes; ACK parsing invalidates the snapshot and resets smoothing. | Reuse metadata; verify that stale replay cannot survive transitions. |
| Stock program identity, private stats disjointness, finite state, ground references and packet framing are current safety boundaries. | Keep those checks for admitted native states as applicable. A native-compatible state is not permission to accept corrupt data. |

Unknowns: exact callback/input equivalence for a living selected adapter, contact
ordering across a native frame, and production replay reset behavior across both
authority changes. These require software evidence. This plan does not assume
that arbitrary customphysics, altered hulls, AD abilities, loadgame, or a changed
QC program are compatible with the stock contract.

These were preimplementation unknowns. The bounded dispatcher and mixed
component checks below now supply evidence for the first stock slice; they do
not establish connected reliability or all stage-2/mod contracts.

## Smallest design versus alternatives

Preferred first slice: recognize a narrowly qualified, frame-start stock native
state (living NOCLIP or FLY with the admitted stock owner/hull), reuse the terminal
queue coalescer, and dispatch one native world frame. Keep selection latched so
the existing completion cursor, pause/resume fences and authority epoch retain
one owner. Resume selected WALK only at a subsequent frame boundary.

This adds state classification and a living branch to an existing adapter. It
must not add a separate native queued simulation or keep duplicate acceleration,
contact, button-latch or replay policy. Factor checks only where needed to keep
receipt, physics and snapshot decisions consistent; finite/framing errors remain
errors rather than native eligibility.

The shared classification is an observational result (WALK, supported native,
terminal, rejected), not a new persistent mode. Receipt and snapshot callers
may permit supported native states; an already-started selected command remains
strictly WALK. Do not widen `SV_PrivateWalkTrialStateError` success to native
types: its post-callback callers continue into `PM_NORMAL`. Do not move its
`SV_CheckWater` side effect into receipt or native eligibility checks, because
native input must observe water at the existing native boundary.

Living native execution is established using the existing frame ownership
discriminator before `SV_ClientThink`. Adapt only the Gorilla eligibility and
deferred-input exclusions that currently mistake latched selection for active
PMove ownership. Native contact processing retains native ordering. Dead-only
pre-respawn draining, Gorilla reset and hand cursor invalidation remain separate.
The new living branch does not set the corpse's `private_move_resume_pending`
exemption or suppress the existing selected arrival-gap checks. General gap
recovery still needs its own coherent change before stage-2 activation.

| Alternative | Disposition before review |
| --- | --- |
| Extend PMove to fly/noclip now. | Defer: QSS-M supports solver modes, but this changes native cheat acceleration/settings and needs a larger behavioral contract. |
| Deselect/reselect clients when cheats toggle. | Defer: resetting admission/queues risks losing accepted input and duplicates mode policy already present in ACK metadata. Review if it is demonstrably smaller. |
| Set only `private_move_native_frame`. | Reject: receipt and snapshot gates still reject the state; stale replay and input ownership remain unresolved. |
| Send every incompatible state to native physics. | Reject for this slice: malformed references, changed QC identity and unsupported customphysics are not a verified stock contract. |
| Run a full fresh native frame after a selected callback changes state. | Reject: duplicates callbacks or movement time; complete the phase/time contract before implementing such a transition. |

The prior mod review's rejection of living fallback applied to a narrow q30 jump
handoff. This proposal reopens that decision for demonstrated stock engine cheat
transitions and leaves mod callbacks out of the first slice.

## Stages and exact owners

1. **Review and qualify frame-start states.** Astra verifies the above contract
   and the alternatives. Define which checks are shared and which remain WALK
   only before edits. Main integrates the review disposition here.
2. **Implement the stock native adapter.** `Quake/sv_phys.c` retains the current
   queue/coalescer, native dispatcher, completion tail and dead continuation.
   Living native frames accumulate only fresh queued roomscale once, retain
   levels/brief latches, replace the angle-only call with `SV_ClientThink` once
   at the native input boundary after establishing native ownership,
   and reset selected timing debt without resetting accepted/completed sequence
   identity. Dead frames retain their current pre-respawn contact clearing.
   Native maintenance with no new command must not repeat impulse or head motion
   or inherit the corpse's indefinite stale-input exemption. `Quake/sv_user.c`
   adapts the two Gorilla selected-owner exclusions to actual frame ownership.
3. **Align receipt and snapshot policy.** `Quake/server.h`, `Quake/sv_user.c`
   and `Quake/sv_main.c` use the same narrow classification. Admission at begin
   remains stock WALK; an already selected valid native state stays connected.
   Snapshots retain selected/completed metadata but report native authority and
   no prediction, including a state change before the next physics frame.
   Reuse existing mode epochs and mandatory owner snapshots. Keep current
   packet validation, paused input fencing and public-peer behavior.
4. **Verify the vertical slice once coherent.** Extend
   `tests/customphysics_native_fixture.c` for callback counts, clocks, queue
   retirement, death regression and native-input equivalence. Extend the mixed
   native fixture (or share its small driver) for actual offer/spawn/begin,
   command production/receipt, server movement, ACK/entity decode and authority
   transitions. Selection must occur through the real begin path, not by
   injecting the selected bit. Existing replay fixture may be extended if the
   combined driver cannot exercise the actual client replay entry point.
5. **Finish stage 2 before changing defaults.** Close remaining stock vertical
   slice gaps: replay, jumping/wet WALK, normal long arrival gaps, pause/resume,
   teleport/pusher boundaries, death/respawn and state changes at each callback
   phase. Determine from actual stock QC which living partial-phase changes are
   reachable; do not claim arbitrary mod support. AD/customphysics/loadgame
   admission belongs to the parent plan's later contracts. Ordinary selected
   movement remains off until its complete software acceptance supports it.

Exact proposed first-slice write set: `Quake/sv_phys.c`, `Quake/sv_user.c`,
`Quake/sv_main.c`, `Quake/server.h`, the named fixtures/make dependencies and
linked implementation documentation. No edits to `pmove.c`, native graphics,
network transport, upstream references, or the user's dirty migration document
are expected. Reopen this plan if a second movement owner, new epoch state,
generic mod fallback or wider solver rewrite becomes necessary.

## Acceptance and failure cases

- Compare a selected native frame with the ordinary native chain from identical
  real stock QC/map state and queued input: origin/velocity, callback order/count,
  due Think window, recoil, brief attack/jump/impulse, roomscale and completion/retirement.
  Include NOCLIP and FLY, both native noclip acceleration modes, no-new-command
  maintenance, two/eight commands, FLY with raw Gorilla hands, and death/respawn
  regression cases. Check that selected WALK still has only its solver owner.
- Execute WALK -> NOCLIP/FLY -> WALK through actual engine command handlers in
  the admitted mixed-peer driver. Verify continued connection, increasing
  completed ACK, native snapshots with prediction off, mode epoch changes,
  owner state matching server, and actual client history/replay reset.
  Carry the complete private movement stats and owner entity through the real
  server-message end before invoking replay. Direct entity decoding alone does
  not commit a usable movement snapshot. Do not clear the command journal
  indiscriminately when reusing the existing metadata invalidation.
- Ensure public desktop peers still use native physics and receive no private
  metadata. Private VR head movement must be consumed once despite redundant
  commands, native maintenance, toggling state and returning to WALK.
- Preserve negative framing/finite/invalid ground/customphysics/hull cases.
  Do not silently convert an invalid state into native dispatch or acknowledge
  movement that did not reach its matching completion boundary.
- Run the Linux build and affected native/parser/replay checks after the coherent
  slice. Avoid repeating unrelated checks after each edit. Captured transport
  proves component integration, not connected signon, packet loss or OpenXR
  device input. Sandbox UDP restrictions and deferred user live testing remain
  explicit evidence limits, not reasons to replace transport.

Windows/ARM qualification, headset/eye testing and performance measurement remain
deferred per user direction. Those exclusions do not make missing implementation
or an unactivated movement path complete.

## Astra disposition

Review provenance: local `gpt-6-astra`, effective `max` effort verified; read-only
review of the committed `2011abaa` draft and named source. Main spot-checked the
load-bearing claims in `sv_user.c`, `sv_phys.c`, `sv_main.c`, `cl_parse.c` and
`cl_main.c`. The [verified brief](predictive-stock-transitions-astra-brief.md)
records the initial scope. No human decision is required.

| Recommendation | Disposition |
| --- | --- |
| Keep selection latched; reuse coalescer/native dispatcher rather than deselect/reselect. | Adopted for the first NOCLIP/FLY frame-boundary slice; one queue and completion owner remain. |
| Establish native ownership before input; fix Gorilla exclusions that equate admission with execution. | Adopted. Gate changes are limited to `SV_GorillaEligible` and `SV_GorillaConsumeDeferredMove`, using existing frame ownership. FLY raw-hand equivalence becomes required acceptance. |
| Replace angle update on the living branch; do not call it in addition to native input. | Adopted after verifying the `DropPunchAngle` call. Recoil is added to equivalence checks. |
| Share observational classification while retaining caller-specific permitted outcomes. | Adopted. Native receipt/snapshot acceptance cannot weaken strict WALK checks in a command already executing PMove. Water observation stays at its existing execution boundary. |
| Do not inherit terminal resume exemptions in living native maintenance. | Adopted for this bounded slice. Existing selected gap behavior remains until a separate recovery change; the parent plan still requires normal gap recovery before activation. |
| Classify current state before snapshot physics, then use existing authority/mode epoch resets. | Adopted. Both pre-physics native authority and qualified WALK return are explicit checks; no new replay protocol is added. |
| Actual replay proof needs complete stats and end-of-message snapshot commit. | Adopted after verifying `CLFTE_CommitMoveSnapshot`. The existing direct entity mixed fixture is baseline evidence only and must be extended for this slice. |
| Keep partial-phase living continuation and broader mod contracts outside this first adapter. | Adopted as staging, not deletion from the parent goal. Production activation remains gated by the complete stage-2 contract. |

The review changed the implementation boundaries and identified concrete VR
movement/recoil regressions that a mechanical terminal-adapter generalization
would have introduced. Production edits proceeded against this revised plan.

## Implemented first slice and evidence

The terminal coalescer is adapted in place as
`SV_Physics_ClientSelectedNativeFrame`; it still invokes the existing native
dispatcher. `SV_PrivateWalkTrialClassifyState` is an observational enum result,
not new persistent state. Shared frame validation and strict WALK execution
validation have distinct permissions. Initial stock admission, selected default
off, private/public framing, command queue, completion cursor and mode epoch
owners remain in place.

Implementation and fixtures are committed as `f128efd3` on `2.0`.

Consolidated checks after coherent implementation:

- Native Linux SDL3 build passed.
- Actual native dispatcher/QC/hull comparisons passed both noclip modes, FLY
  and FLY/raw hands with zero/two/eight commands and a subsequent no-packet frame.
  Origin/velocity/recoil match the existing native chain; callback order/count,
  clocks, once-only recoil/roomscale/impulse, completion/retirement and latest
  levels are checked. These callbacks are diagnostic QC, not stock gameplay.
- Real stock-QC mixed component run passed actual private admission, synthetic
  client command production/receipt, engine cheat handlers, authoritative physics,
  complete stats/ACK/entity message parsing, and actual client replay after both
  WALK returns. Four authority changes advance the existing mode epoch, native
  authority is observed before physics, and stale replay propagation is cleared.
  The simultaneous public desktop peer stays native and moves/fires. Unselected
  mixed native behavior passes separately.
- Negotiation defaults/public fallbacks and demo header/entity checks passed.
  The staged selected demo producer now supplies the stock hull expected by
  current classification; it still does not prove admission.
- Existing selected pause/resume fixture and real installed q30 movement
  comparison passed. Pause qualification uses its stated physics seam; q30
  selection remains injected in that comparison and unadmitted in production.
- The selected mixed run also passes with AddressSanitizer/UBSan instrumentation
  on its included production parser/server-writer sources. The other linked
  engine objects use the normal build; this is not whole-engine sanitizer proof.
- Pure qualification/strict WALK negative cases passed non-finite state,
  unsupported hull/customphysics, and invalid integer ground offsets. The
  stored water category is not mutated by the new observational path.

The mixed fixture initially exposed a headless-only skin upload crash when full
message parsing used prepared client resources. It now explicitly captures
`R_TranslateNewPlayerSkin`, since dedicated model loading supplies no renderer
textures. Real physics, movement stats, message parsing and replay remain the
executed owners. UDP and ptrace are unavailable in this sandbox; connected
signon/reliability, actual XR device input and graphical output are not claimed.
Fixture commands and evidence limits are in [tests/README.md](../tests/README.md).

The local Astra implementation pass used fresh, explicitly selected
`gpt-6-astra`/`max`, verified effective settings. A resumed reviewer whose runtime
changed settings was stopped and its result was not credited as review.
The completed pass found no blocking defect in the bounded production adapter;
main spot-checked the ownership lifetime, strict WALK guard, completion tail
and pre-physics snapshot claims.

| Implementation-review recommendation or limit | Disposition |
| --- | --- |
| Native ownership is established before input and cleared at WALK execution entry. | Verified and retained; the Gorilla exclusions use this existing discriminator. |
| Coalescing, native clock and successful completion retain existing owners. | Verified and retained; the dispatcher comparisons also execute empty-queue maintenance. |
| Observational frame validation must not authorize native types inside PMove. | Verified and retained; strict WALK negative checks pass. |
| Snapshot classification plus retained frame discriminator conservatively handles both transitions. | Verified and retained; real engine command/complete-message checks cover pre-physics and WALK return. |
| Static review cannot establish runtime equivalence or complete-message replay. | Addressed with the bounded native and mixed checks above; connected/hardware/graphics claims remain excluded. |

Remaining parent stage-2 work: normal long-gap recovery, complete dry/wet/jump,
pause/death/respawn/teleport/pusher integration and callback-phase contracts before
automatic selected movement. Broader AD-family/cooperative mod contracts and
the full migration matrix remain required by the parent plan. This first slice
does not complete predictive movement or the full migration goal.
