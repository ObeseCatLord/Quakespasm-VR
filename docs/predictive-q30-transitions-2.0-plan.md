# q30 native states, transitions and normal admission

Status: preimplementation plan for the remaining stages of
[ordinary q30 replay](predictive-q30-replay-2.0-plan.md). Policy transport and
the replay consumer are committed at `8a0871c1`; normal q30 admission remains
closed. This plan retains the full [AD-family outcome](predictive-mod-admission-2.0-plan.md).

## Behavior and reference

A normally admitted private desktop or VR client predicts qualified ordinary
dry q30 movement and survives startup, boots, grapple, ladders, water, authored
holds, cameras, hull/movetype changes, death and return. Public desktop players
retain vkQuake's native QC/physics and can share the world. Native-only states
retain selected input/ACK ownership without pretending to be predictable.
Shared weapon and muzzle offsets remain the only calibration.

Use the exact installed q30a1024 program and native vkQuake phase order as the
behavioral reference; ordinary command movement intentionally uses the already
reviewed shared PMove integrator. A normal session must demonstrate capability
offer, actual spawn/begin, real QC movement, completion, complete serialized
stats/owner, full client parsing and replay. Component-injected selection does
not meet that outcome.

## Verified state and reusable owners

The [Astra brief](predictive-q30-transitions-astra-brief.md) separates source and
bytecode findings from unknowns. The exact program is pinned by loader SHA-256.
Its typed names are available through VM-owned `ED_FindField`/`ED_FindGlobal`;
do not embed field/global slots or add a binding cache. Bounds-check definitions
before dereferencing: `GetEdictFieldValue` checks a negative offset only.

| q30 state evidence | Required disposition |
| --- | --- |
| `prethink`/`postthink` initially zero; `chaoscount` startup runs while <=2. | Fresh native startup, preserving authored initialization/restart effects. |
| `intermission_running`, `secloc_running`, `cinematic_running`; camera activation can precede its cinematic flag. | Native camera/freeze eligibility, observing actual globals rather than maintaining a second camera lifetime. |
| `skill != oskill` initializes/resets evil mode. | Native for the transition before its side effects. |
| Jump boots mask1048576; grapple upgrade mask128, `hookent` and hook class190. | Native ability eligibility before forces, rather than subtracting or emulating their writes. Structurally validate entity offsets first. |
| Super shotgun weapon2 can subtract forward*50 from airborne velocity. | Native weapon eligibility before firing; switching into it after movement stops batching. |
| `onladder` latch clears before damping/jump; triggers rearm it. | Native ladder ownership and existing Gorilla ladder latch; no inferred perpetual0.9 damping schedule. |
| `oldgravity` is written into `gravity`, or1 when nonpositive. | Ordinary replay requires a coherent gravity seed. A pending normalization stays native until the current seed matches. |
| Future `pausetime` zeros velocity after ordinary jump. | Native hold; prevent deferred Gorilla input from adding movement after the authored zero. |
| Nonempty `target2` invokes arbitrary targets from PostThink. | Native until the authored target is consumed; do not replay arbitrary target effects. |
| q30 waterjump writes flag/deadline; water drag is authored QC. | Native liquid/waterjump domain; the stock timer adapter must not clear these writes. |
| T_Damage's knockback branch at37638–37657 requires non-world inflictor and WALK; ordinary burning/poison use world. | Do not exclude debuffs merely from their function names. Qualify actual cadence/effects and handle resulting death through the existing terminal boundary. |
| Valid NONE/FLY/NOCLIP/TOSS/BOUNCE/GIB or changed living hull. | Existing native dispatcher, retaining valid sessions. Unknown types, nonfinite body state and malformed references remain rejected. |

Reuse current selection, queue, credit, completion/retirement, contact cursor,
native fresh/phase dispatcher, QC callbacks, world Think window, snapshot mode/
discontinuity epochs and permission. Receipt and snapshot classification must
stay observational; only the physics turn performs fresh water categorization.
Initial admission is before `spawned=true`; reuse a common internal body
predicate with the existing active/known-to-QC lifecycle, without temporarily
changing `spawned` or creating another session lifetime.

Minimal adapter versus rewrite: one typed current-state predicate and extensions
at existing phase boundaries preserve the working movement/QC owners. A second
native command solver, force emulator, queue, completion cursor, field cache or
QC scheduler duplicates existing policy and is outside this plan. An optional
roomscale lookahead is still a design question, not permission to add a general
transaction framework.

## Stages, ownership and dependencies

1. **Pure classification and fresh native execution.** `sv_phys.c` implements
   typed/bounded current-state qualification and a narrow authored hold/camera
   predicate. Its existing enum remains the interface to receipt, dispatch and
   snapshots. `server.h` exposes only required existing-boundary helpers.
   `sv_user.c` uses the hold/camera predicate at existing Gorilla eligibility/
   deferred-input boundaries. Existing physical contact validation drains
   invalid hold/camera samples through its current cursor. Add no persistent
   capability/ability state. Admission remains closed at this checkpoint.
2. **Resolve and implement phase handoffs.** Local Astra reviews the mostly
   worked brief before the subtle boundary is implemented. A dry command can
   enter liquid through pre-QC roomscale; fresh native input must precede QC.
   Prove a minimal existing-sweep lookahead or a smaller adapter, including
   weapon-pose/push-grid/link side effects. Reject late ClientThink, repeated QC
   or an extra whole-world interval. If a later batch head needs native before
   callbacks, preserve it and stop batching; do not append a native frame after
   an earlier head's movement. Before-movement callback transitions require
   explicit remaining-phase contracts. After movement, finish PostThink and
   current completion once, clear credit/hand continuity, stop batching and
   preserve later heads. Guard stock waterjump correction from q30 writes.
3. **Normal admission and publication.** `sv_main.c` admits the pinned program
   only with its negotiated policy, disjoint current customstats and complete
   native contract. Reuse existing spawn/begin/reset owners. Grant QC_COMMAND
   replay only from qualified current dry state plus complete finite movement
   inputs and representable velocity; native snapshots keep legacy authority.
   Keep existing pusher/palm, timer, pause and input-phase fences. Stock/public
   protocol behavior stays in its current owners.
4. **Consolidated vertical qualification and final review.** Extend existing
   q30 movement, negotiation/mixed-session and replay fixtures to cover normal
   admission/return, traversal and actual serialization/parser/replay. Run the
   Linux build and meaningful local checks after coherent implementation, then
   final local Astra review and disposition. Do not claim selected all-native
   startup, a dormant helper or an injected jump comparison finishes the feature.

Exact production write set: `Quake/sv_phys.c`, `sv_user.c`, `sv_main.c`,
`server.h`, and a demonstrated missing consumer at the existing `pmove.c` /
`cl_main.c` boundary only after recording it. No new wire layout is planned.
Verification uses `tests/q30_movement_native_fixture.c`, existing negotiation/
mixed native and owner/replay fixtures, their current make owners and README.
Do not add a replacement bootstrap or network simulator. Main owns integration;
coding delegation must use available user-requested routes and disjoint writes.
The unrelated user-edited migration document remains outside all write sets.

## Acceptance and reopening criteria

Require actual initialized dry replay, held landing/re-jump, changed/zero/
clamped heights and repeated preview; real-QC boots/grapple/ladder/water/hold/
camera/terminal paths and return; entered-during-roomscale cases; callback-phase
changes without duplicated QC, input, movement duration or completion; later
queue heads retained at handoff; hold/camera Gorilla and physical-contact
qualification; missing/typed/nonfinite/reference failures; old/unknown policy,
customstats collision, reconnect and map reset. Distinguish staged predicate
tests from actual map-trigger traversal and injected transport from ordinary
spawn/begin. Keep stock/public mixed-world checks passing.

Live headset/eye tests, performance measurements and Windows/ARM qualification
remain deferred by the user. They do not excuse missing software behavior.
Wider AD/Mjolnir and cooperative-QC integration remain in the parent plan.

Reopen before implementation exceeds one current-state predicate and existing
phase adapters, requires a second scheduling policy, cannot retain native input
order, or needs repeated repairs among new layers. The remaining phase decision
is under Astra review; record its disposition here before coding that boundary.
