# Selected stock movement on moving brushes

Status: preimplementation plan; baseline `a6ce3246` on `2.0`. This is the next
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
