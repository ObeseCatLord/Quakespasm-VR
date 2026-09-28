# Selected stock movement on moving brushes

Status: bounded adapter implemented; local software checks and final Astra
review pass, with scope and remaining parent stages recorded below. Baseline `a6ce3246`
on `2.0`. This is the next
stage of [production predictive movement](predictive-movement-2.0-plan.md).
The full outcome remains ordinary stock/mod desktop and VR gameplay, with
prediction where its actual movement contract matches. An opt-in exact-stock
test is a stage, not the finished feature.

## Behavior and reference

An admitted stock player can ride, leave and jump from a lift, be pushed by a
door, and plant a Gorilla palm on a brush without disconnecting or doubling
platform displacement. Native vkQuake plus the already integrated robust
pusher-support owner is the authoritative brush/callback reference. QSS-M
PMove remains the selected body solver; the inherited shared Gorilla solver
and its surface-local anchors remain the hand reference. Public/native players
and desktop graphics retain their owners. Replay is withheld for actual
moving-brush interaction until a matching replay contract is qualified; server
support and command completion remain functional during that interval.

## Verified evidence and unknowns

| Source evidence | Consequence |
| --- | --- |
| `sv_phys.c:SV_PrivateWalkTrialStateError` and PMove ground/touch processing mark body pusher interaction; default `sv_gameplayfix_elevators=3` accepts it. | Body support is already implemented, but actual selected-stock lift qualification is missing. Do not replace it because the previous liquid test prepared only a pusher marker. |
| `SV_PushMove` marks affected selected players and uses the existing support record/carry/rollback owner. `SV_Physics` still runs that world dispatcher. | Preserve one native brush advance and rollback path; no command-time brush simulator or second support record. |
| Snapshot permission excludes `private_pmove_pusher_interaction`. | Reuse this existing conservative replay boundary for hand contacts too; contact acceptance does not imply replay permission. |
| Selected palm checks reject every `MOVETYPE_PUSH/SOLID_BSP` contact, including stationary brush entities. | A valid gameplay contact is currently a disconnect, not malformed input. This is the demonstrated narrow incompatibility. |
| `pmove.c:PM_GorillaSurface` transforms brush anchors through current origin/angles and model identity; `vr_gorilla.h` stores/restores surface-local anchors. | Reuse the existing geometry/anchor owner. Do not invent a retargeter, hand-support state machine or wire payload. |
| `stock_liquid_native_fixture.c` already executes actual admission, send/receipt, pinned QC, BSP physics, full snapshots and live replay. | Reuse its bootstrap/captured transport/prepared resource seams rather than build another signon driver. |
| Stock e1m1 BSP contains real `func_plat` models `*7` and `*22`; e1m2 contains trains and a plat. | First vertical proof should use the installed pinned stock QC and real lift geometry/callbacks. |

Unknown: selected/native carry agreement across lift activation, stopping,
blocked rollback and jump-off; command batching and quiet-frame support;
fresh/retained raw palm contacts on that brush. Transient PMove displacement
must be distinguished from native carry. Captured transport and generated hand
input do not prove connected/device behavior. Legacy elevator modes below 3
remain separate qualification, not a reason to redesign default support.

## Adapter comparison

Preferred: qualify the existing body path; replace only the palm brush-contact
veto with marking the existing interaction flag under the already supported
robust elevator mode. Keep invalid/out-of-world/freed contact rejection and
unsupported legacy modes restrictive. Existing PMove surface transforms own
moving anchors; existing `SV_PushMove` owns brush movement and body carry.
If actual proof finds double movement, fix that specific boundary rather than
route every brush-adjacent WALK command into a fresh native frame.

Rejected: a second moving-platform simulator would duplicate world time,
blocked QC, support records and rollback. Blanket native fallback after PMove
would repeat QC/movement time; selecting native at frame start based on a
large proximity envelope changes working body behavior without evidence.
Predicting a moving brush from its velocity alone omits QC stops/rollback;
withhold permission through the existing flag for now rather than assert parity.

## Stages and ownership

1. **Qualify current body support.** Add one fixture importing the liquid/mixed
   bootstrap. Actual `begin` must select the owner, ordinary generated commands
   must complete, and real stock plat QC must advance through the world loop.
   Prepared starting position/support is explicit; do not set selection,
   ACK, permission or mover velocity/Think to manufacture admission/motion.
   Compare separately initialized native/selected observations; check exactly
   one carry using brush-relative body displacement, completion/queue retirement,
   full-snapshot replay denial and replay return after leaving support. Use
   quiet frames and batching. A prepared physical obstacle may qualify rollback
   if no natural stock obstruction is reachable, and must be labeled as such.
2. **Adapt palm eligibility.** Main owns the narrow `sv_phys.c` contact loop.
   Finite live brush contacts in robust mode mark the existing pusher flag
   instead of disconnecting. Keep solver, callback, credit, queue, timer,
   surface identity and completion owners. Reopen the architecture if supporting
   these contacts requires another persistent state or movement phase.
3. **Qualify hand and transition behavior.** Extend the same actual driver with
   serialized generated VR/raw Gorilla commands, fresh/retained brush contact,
   moving-anchor observation and normal release/OFF. Require actual acceptance
   and command completion, no disconnect and no unauthorized live replay.
   Check return to ordinary replay using full authoritative state. Invalid
   references and unsupported elevator modes must retain their boundaries.
4. **Review and integrate.** Consolidated local Linux build and relevant
   fixture/sanitizer checks after the coherent slice, followed by local Astra
   source review. Update parent plan/index and commit explicit files on `2.0`.
   Default selected activation, load/local-SP and AD/cooperative-QC stages
   remain required next work; this slice does not close them.

Delegation contract: one coding agent may own only
`tests/stock_pusher_native_fixture.c`. It is not alone and must not revert other
work. Main owns plan, production, test make dependencies, README and integration.
No writes to `docs/migration-2.0.md`, main or runtime installation. If the
authorized Luna route is absent, an explicitly authorized single web coding
route may be tried; do not silently substitute another local coding model.

## Acceptance and limits

Actual real-map lift motion must be visible in brush and player origins, with
support/carry checked through actual physics; counts/packets alone are not proof.
Capture source/client full snapshot and actual replay decisions in contact and
release states. Stock environmental QC and brush callbacks remain actual.
Compare native and selected gameplay/carry obligations rather than forcing
their intentionally different movement integrators to have identical airborne
trajectories. Any tolerance must derive from a specific rounding/contact rule;
do not excuse unknown drift with a broad radius.

Use generated desktop/VR input, 25 ms first, then relevant short/long commands,
queued commands and quiet frames. Preserve existing liquid/pause regression
acceptance. Renderer/headset/eye testing, performance measurement and Windows/
ARM qualification stay deferred by the user. Senior-review disposition follows
before production edits; implementation evidence follows after completion.

## Astra design disposition and amended boundary

Local `gpt-6-astra/max` read-only design review, effective fields independently
verified from its latest `turn_context`. Main checked the source reset,
maintenance and contact/permission lifetimes. Stage 1 prerequisite passes for
actual e1m1 lift activation: separately initialized native and selected runs
both rise 152 units over 39 moving world frames, including one quiet carry and
one batched carry, then jump/release; selected completion and full-snapshot
replay return pass. This does not prove palm-only or rollback behavior.

| Recommendation | Disposition before production edits |
| --- | --- |
| A palm-loop mark can be lost on quiet frames without body contact. | Adopted. `SV_Physics_Client` resets the frame flag, and maintenance marks body ground only. Add a pure snapshot eligibility check against live/model-matched retained palm bindings, using existing state; no new persistent flag or hand owner. Main's production write set now also includes this narrow `sv_main.c` predicate. |
| Local anchors do not imply passive palm-only carriage. | Adopted. Preserve the inherited physical-stroke budget; test foot-plus-palm support separately from palm-only binding/quiet/stopped/batched states and intentional strokes. Do not promise automatic hand-only lift riding or spend externally supplied motion as a hand stroke. |
| Lift riding does not qualify non-rider door pushing or rollback. | Adopted as required acceptance. Add actual stock brush push and actual blocked-QC/reversal observations with explicitly prepared physical obstruction if needed. Keep support/rollback/callback owners. |
| Check stale-result invalidation after callback relocation/replacement. | Adopted. Reuse existing reset-generation, model identity and relocation/publication guards; add focused composition evidence rather than another lifetime store or extra palm impacts. |
| Existing architecture is sufficient if those contracts pass. | Adopted conditional adapter. Preserve authority versus replay permission and restrictive legacy modes; reopen if another movement phase/state owner becomes necessary. |

The single authorized web coding attempt failed before edits when its browser
could not open; no replacement local coding model was selected. Main implements
the bounded fixture. This operational failure does not change feature scope.

## Implementation and software evidence

Production adds the reviewed palm eligibility delta, pure snapshot predicate
and candidate validation at the existing publication tail. The shared
local-anchor/PMove, world pusher/support/rollback, QC,
queue/completion and wire owners remain unchanged. Current robust mode accepts
live brush contacts and withholds replay for fresh interaction or a retained
model-matched planted palm. Legacy-mode and invalid/freed-reference guards
remain; default selected movement stays off pending parent stages.

The actual raw hand test first exposed omitted dedicated-client registration
and reliable storage in the prepared harness. After those ordinary resources
were supplied, the real server offer/client handler/queued capability/raw codec
reproduced the original unsupported-pusher disconnect. Prepared headless
disconnect resources subsequently faulted; that run establishes the emitted
disconnect, not a clean connected negative lifecycle. The corrected adapter
then completes the same real contact path without dropping the player.

Normal consolidated acceptance uses one imported actual-code fixture:

- Actual e1m1 lift `*7`: independently initialized native/selected 25-ms runs
  both rise 152 units over 39 moving frames, one quiet carry and one batched
  carry. Generated VR and selected 10/100-ms runs pass (100/10 moving frames).
  Actual jump releases support and complete snapshots restore replay.
- Foot-plus-palm binding: 19 moving contact frames with an explicit subthreshold
  stroke, quiet and batched commands. Native carry records match actual brush
  movement, local anchor height follows brush/body, and OFF clears state.
- Palm-only on actual static floor beside the lift: stationary and moving quiet
  frames have interaction flag zero, unchanged completion, RUNNING/unpaused,
  zero waterjump/no external hold, but actual full-snapshot replay remains off.
  Command batching and 10/25/100-ms subthreshold strokes pass; another public
  player activates actual lift QC. No passive palm carriage is claimed. OFF
  clears the binding and actual replay returns.
- Prepared physical ceiling with ordinary bbox size: actual stock `plat_crush`
  applies one damage, reverses the lift and rolls back body/brush positions.
  Native/selected and selected-with-palms cases pass; anchors stay consistent.
  Removing the wall allows ordinary reversed motion. Callback effects are not
  expected to roll back with geometry.
- Prepared trigger/destination with actual installed `teleport_touch` during
  selected movement: epoch/reset generation advance; stale PMove hand output
  is not republished into server/client baseline or replay permission.
- Actual targeted e1m1 door `*17`: prepared invocation of installed
  `door_go_up/down`, without mover velocity/Think field writes. Native/selected
  real brush contact displaces a non-rider within brush-direction/motion bounds
  with support carry record zero. Full snapshots deny selected replay; ordinary
  relocation/command completion restores it. This is not map button progression.
- Actual registered QC `setmodel` builtin and actual `ED_Free`, separately from
  palm-only binding: existing reset/invalidation hooks clear the state; full
  snapshots have no stale raw ACK and actual body replay returns. Builtin
  parameters/argc are prepared through the existing registry, not simulated QC.

Carry comparisons allow only the explicit 1/8 PMove position nudge plus hull
`DIST_EPSILON` and small float arithmetic. Deliberate hand travel is explicitly
subtracted from the foot-plus-palm carry oracle. No broad trajectory tolerance
or artificial selection/ACK/permission is used. Prepared starts/resources/
registration/input, physical obstacle, trigger/destination, callback activation,
builtin arguments and longer outer world clocks remain documented component
seams. Real installed QC/BSP/world/collision/message/parser/replay owners execute;
this is not connected/device or arbitrary-QC/full-domain proof.

## Publication boundary reopened before correction

The implementation review identifies a narrower lifetime interval than the
already-published surface tests: PMove can acquire a new brush binding, then a
callback changes/frees that brush before the candidate state is published.
Main verified `SV_GorillaInvalidateSurface` scans only current client bindings;
the pending local `result_gorilla` is not there yet. Reset-generation and body
relocation alone therefore cannot validate that new result's surface identity.
This is a demonstrated source incompatibility, not a missing-test justification
for a new lifetime owner.

Amended minimum: at the existing result-publication boundary, verify each
non-world candidate anchor still names a live solid brush with the matching
model. If not, invoke the existing accepted-state invalidation/cutoff owner
instead of publishing stale output. Do not roll back movement/QC, add a
persistent surface generation or rerun hand physics. Main's production write
set remains `sv_phys.c`/`sv_main.c`. Add only a fixture callback-composition hook
after actual QC execution in the reused `stock_liquid_native_fixture.c` wrapper;
new pusher variants use actual registered setmodel/ED_Free during actual
PlayerPostThink, checking fresh acquisition before publication. Existing
published-binding tests remain valid and need no speculative rerun.

Acceptance: first reproduce stale candidate publication in this exact interval;
then require existing reset/cutoff, no stale server/client raw baseline and
working full-snapshot body replay after replacement/retirement. Keep prior
relocation, carry, callback-effect and permission contracts. Astra reviews
the final guard and this bounded seam after the correction.

The bounded fresh-contact reproduction failed before that guard: server state
remained initialized/touching with a raw baseline after actual PostThink and
prepared actual model replacement. The reset-generation observation includes
the preceding ordinary RESET command; it is not evidence that the mutation
itself advanced the generation. After the guard, both replacement/retirement
cases clear initialized/touching state and the raw baseline through the existing
invalidation owner. The current frame's pusher mark still withholds replay;
the subsequent quiet full snapshot restores actual replay permission.

## Final Astra disposition and integration evidence

Local `gpt-6-astra/max` reviewed the final production changes, actual fixture
and publication/permission boundaries read-only. Main independently verified
the latest model/effort fields and the load-bearing source evidence. The review
accepts the bounded pinned-stock implementation with no remaining demonstrated
source blocker, subject to the final build/pending sanitizer checks; main
inspected those checks passing.

| Finding | Final disposition |
| --- | --- |
| Fresh contact can be replaced/freed before entering accepted client state. | Resolved for the reproduced cases. Candidate liveness/solid brush/model checks at `SV_PrivateWalkTrialGorillaSurfacesValid` supplement the existing generation/relocation fence. Failed publication calls existing invalidation; command completion/credit and committed QC/body effects remain intact. Both new PostThink composition variants pass. |
| Quiet retained palm bindings can outlive the frame interaction mark. | Resolved. Keep the pure accepted-binding snapshot predicate and transient frame mark: they answer different lifetime questions. Actual stationary/moving palm-only quiet tests exclude pause/timer/hold/body contact as alternative explanations. |
| A new hand or platform owner would duplicate working behavior. | No new owner needed. Shared local anchors, stroke limits and native carry/rollback/QC remain unchanged. Separate small publication-validity and replay-eligibility predicates have different purposes and need no broader abstraction. |
| Same-slot/same-model destruction and recreation during one callback is not qualified. | Retained as an explicit mod-lifetime assumption. Entity/model identity is unchanged by this slice; do not claim arbitrary-QC qualification or silently add a second surface-generation system. Reopen the narrow boundary if a supported mod demonstrates that incompatibility. |

Software evidence: 16 normal component cases cover the unchanged body/native,
generated VR, command-duration, hand, rollback/teleport, door and published
surface-lifetime paths; two additional normal fresh-contact variants exercise
the final publication guard. Seven focused partial-instrumentation ASan/UBSan
cases passed before the guard; six final cases cover pending replacement/
retirement, planted palms, blocked/teleport and published replacement/retirement
with the guard. These are overlapping sets, not thirteen unique sanitizer cases.
The exact final Linux SDL3 `-Werror` build passes. Existing wet/ledge/causal-pause
and mixed selected/public startup/arrival-gap drivers pass. No broad unchanged
matrix rerun was needed after the narrow publication correction.

The [fixture instructions](../tests/README.md#selected-stock-moving-brush-adapter)
and [review brief](predictive-stock-pushers-implementation-astra-brief.md) record
the real owners and explicit seams. This finishes this planned adapter only.
Full-domain stock/default selection, local/load behavior and AD/cooperative-QC
admission remain parent implementation stages; live/device, performance and
Windows/ARM qualification retain the user's deferral.
