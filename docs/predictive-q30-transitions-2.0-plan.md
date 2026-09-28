# q30 native states, transitions and normal admission

Status: implementation in progress. This plan preceded the typed-classification
and bounded-dispatch checkpoint of
[ordinary q30 replay](predictive-q30-replay-2.0-plan.md). Policy transport and
the replay consumer are committed at `8a0871c1`; bounded normal admission now
passes the [activation slice](predictive-q30-activation-2.0-plan.md), consolidated
Linux checks and final local Astra review. Real ability/trigger traversal and
broader mod compatibility remain open. This plan retains the full
[AD-family outcome](predictive-mod-admission-2.0-plan.md).

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
stay observational. Physics may use fresh water categorization to choose an
owner, but must restore those speculative values before native input/QC: native
PreThink historically sees the previous water state, with native WALK refreshing
it later. The probe must not introduce a new QC observation order.
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
Share the existing stock liquid-position finder in a small
`tests/native_liquid_fixture.h` between the stock and q30 drivers rather than
copying a new map search. The horizontal-crossing fixture may enumerate later
matches through a caller-owned ordinal: the first wet point need not have a
reachable dry neighbor. Retain the stock first-match wrapper and its exact
lookup/restoration algorithm; add no second geometry search. Update those
existing make dependencies. This is a fixture helper only, with the same actual hull/content lookup and restoration;
it does not add a bootstrap, collision owner or transport simulation.
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
order, or needs repeated repairs among new layers. The disposition below resolves
the roomscale decision. Reachable callback closure and normal-session
qualification remain admission prerequisites.

## Local Astra design disposition

One read-only local Astra Max review verified the actual native/selected phase
order and pinned q30 image. Main independently verified effective
`gpt-6-astra` / `max` turn metadata, area-list insertion and equal-fraction
collision selection, push-grid sorting/deduplication/live checks, the weapon
scope's link-only hook, and the auxiliary call chain's lack of persistent
pusher-support writes. No raw operational telemetry is part of this artifact.

| Recommendation | Disposition |
| --- | --- |
| Reuse the callback-free roomscale sweep with bounded local restore. | **Adopted.** Probe only at the synchronous physics dispatch boundary, outside QC/weapon-pose scopes. Use the candidate head's duration and existing sweep/step behavior. Restore command, duration, origin, velocity, flags, ground, water, bounds/PVS and the exact original area-list position, including initially unlinked owners. Ordinary relinking alone changes collision tie ordering. |
| Do not add a grid, weapon-scope or pusher-record transaction framework. | **Adopted.** Existing grid entries are a conservative superset and are sorted/deduplicated/live-tested; the restored link recomputes live bounds. No active weapon scope is allowed at this boundary. The auxiliary chain does not write persistent pusher support. Reopen if a new writer is added. |
| Choose fresh native before command staging, angles or QC. | **Adopted.** Offset0 can invoke the existing fresh native/coalesced owner once after restoring probe state. At offset>0, stop before the unstarted head, set the existing native-frame flag, clear credit and preserve it for the next world pass. Append no native world interval to earlier command movement. |
| Preserve native QC-visible water order. | **Adopted; brief corrected.** Speculative water selects dispatch only. Restore previous water before native ClientThink/roomscale/PreThink; native WALK retains its later refresh. No new gameplay water clock. |
| The probe does not solve arbitrary callback entry. | **Adopted.** q30 admission stays closed until ordinary PreThink/scheduled Think closure and explicit residual transitions are proved. Full ClientThink after QC and unconditional PM_NORMAL are not valid fallbacks. Whole-world continuation is not a generic living-q30 escape hatch. Maintenance has the same proof requirement without new input/time. |
| Keep native qualification loss sticky after movement. | **Adopted.** Observe at movement/contact boundaries, retain the existing frame-native flag even if PostThink returns to WALK, complete the current head once, clear credit and stop batching. |
| Cancel deferred Gorilla movement on holds/cameras. | **Adopted.** False eligibility alone makes the deferred consumer fall through to ordinary input. Cancel at its existing early-return boundary; physical contacts invalidate continuity while advancing the existing cursor. Keep stock waterjump clearing away from q30 ownership. |

The implementation review found four concrete handoff defects. Its disposition
changes no movement owner: observational frame validation precedes a probe;
resetting hand continuity fences only completed work, preserving unstarted
samples; sticky batching suppression is separate from current-owner validation.
Native geometric eligibility is evaluated from the accepted head before command
credit, so a deferred head needs no new credit/lifetime flag to enter the next
fresh native frame. Native execution already uses world duration and coalesces
accepted input independently of command-time credit. Ordinary dry maintenance
still uses only completed levels and receives no uncompleted head. This is
smaller than a second pending-native scheduler/latch. Verify that distinction
in the final code review and retained-head checks.

The review changed the restoration and deferred-input contracts. The bounded
checkpoint has since passed final local Astra implementation review and focused
Linux acceptance, with a separately reproduced stock-liquid failure recorded
in the [implementation review](predictive-q30-transition-implementation-review.md).
At that checkpoint, callback closure, traversal and normal admission acceptance
remained required. Subsequent bounded closure/traversal work and the
[normal activation slice](predictive-q30-activation-2.0-plan.md) are now qualified;
real ability/trigger traversal and wider-mod compatibility remain required.

## Implemented checkpoint and next work

Typed current-state and pre-begin predicates, native hold/contact boundaries,
dispatch-only water/roomscale lookahead, later-head deferral and sticky
after-movement completion are implemented in the existing owners. The hand
reset preserves an existing relocation cutoff while retaining unstarted raw
samples. Normal q30 admission and replay permission remained closed at this
checkpoint; the later activation slice above connects the qualified session.

The exact-q30 fixture passes actual native startup/ordinary return, staged typed
ability/reference cases, actual QC hold cancellation, valid-versus-held contact,
real-BSP probe restoration, native water observation order and paired retained/
invalidated raw samples. The seven existing ordinary replay comparisons pass.
Linux production build, stock/q30 negotiation and stock mixed-session checks
pass. Stock water/VR at 25 ms passes; its 10 ms ledge assertion reproduces with the
pre-checkpoint physics/input sources and remains unresolved. Component fixtures
do not establish actual horizontal water entry or normal q30 admission.

This checkpoint's next stages addressed living qualification loss from PreThink
or scheduled Think using existing remaining-phase owners, bounded ordinary
callback closure and real roomscale traversal, followed by the normal session
vertical. Their later records and activation qualification retain the broader
ability/trigger requirements. Revise and commit the plan before expanding that
contract; do not introduce generic late ClientThink, another world interval or
a second movement scheduler.

## Callback-phase design disposition

The subsequent [mostly-worked brief](predictive-q30-callback-phase-astra-brief.md)
was reviewed read-only by local Astra with verified `gpt-6-astra` / `max`
settings. Main spot-checked the duration, toss scheduling, deferred-input and
contact-processing findings against the existing sources.

| Recommendation | Disposition |
| --- | --- |
| Choose actual callback closure before adding a generic living residual. | **Adopted.** No living native continuation, shadow input calculation or force reconciliation is authorized by uncertainty. Preserve ordinary shared PMove and existing terminal/stock-freeze behavior. |
| A final QC zero does not prove native input equivalence. | **Adopted.** Native friction/acceleration precedes QC; earlier observations and retained vector components matter. A class-preserving force could evade a classification-only fallback. |
| Command-duration or zero-duration native continuation changes scheduling/physics. | **Adopted.** TOSS's Think horizon and zero-time WALK unsticking invalidate that shortcut. No maintenance native sweep merely because duration is zero. |
| Terminal contact draining cannot be reused as living retirement. | **Adopted.** Living samples can execute attacks; deferred input survives a basic hand reset, and relocation cutoffs must stay monotonic. No new cursor/retirement owner. |
| If a specific unsafe scheduled callback is demonstrated, qualify it before staging through the existing dispatch/defer boundary. | **Adopted conditionally.** Establish exact identity and reachable effect first; observe the existing world Think opportunity/deadline without consuming it. Prove PreThink cannot replace or newly schedule unsafe Think. Do not classify all animation/firing as native or broadly weaken StateError. |
| Preserve native pre-Think dispatch and existing support guards. | **Adopted.** Current-mode dispatch is not a generic substitute for native type capture. A different pre-QC move-frame snapshot needs a demonstrated incompatibility, not speculation. |

Main redecoded actual PreThink1143 PCs54204–54431: startup/camera/skill/boots/
grapple/ladder paths have current-state exclusions. Ordinary calls include
CheckRules54305, WaterMove54306 and PlayerJump54420. NextLevel1137 schedules a
different entity; ordinary dry WaterMove returns before liquid damage. This is
partial closure evidence: attachment and all indirect/scheduled callback paths
still require verification. Source-like reflected-axe naming was investigated
and rejected as a movement-force claim: actual Resist_Axe865 PCs35054–35090
contains effects/sound/smoke, not a demonstrated player-body force.

Continue with actual horizontal roomscale entry and later-head deferral proof
using the existing dispatch implementation. Different stock BSPs may be selected
by the existing fixture bootstrap; use an explicit map option and distinguish
prepared airborne starts from grounded ledge or authored trigger traversal.
No production gate is justified until an unsafe callback is demonstrated.

The [callback closure record](predictive-q30-callback-closure-review.md) now
closes ordinary PreThink and demonstrates an actual scheduled third-axe HP-target
camera transition. Its next contract qualifies only those five due callbacks
before staging using the existing world Think opportunity. That bounded plan
precedes production changes; other scheduled callbacks and normal admission
remain open.
