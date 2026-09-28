# AD-family predictive movement: admission and ownership plan

Status: Astra-reviewed architecture and implementation in progress; ordinary
q30 policy transport and client replay are implemented at `8a0871c1`, with
qualified component evidence. Normal q30 admission and complete native
transitions remain unimplemented. The stock default activation
slice does not admit q30 or finish mod prediction. This plan expands stages 3–4
of the [parent movement plan](predictive-movement-2.0-plan.md). The disposition
below chooses the existing command owner; the next bounded proof and matching
replay contract must precede production admission. Unresolved questions are not
permission to invent a second movement owner during implementation.

## Outcome and reference

Ordinary private desktop and VR players should use modern sequenced movement
with supported AD-family gameplay, sharing a vkQuake world with public desktop
players. Preserve map-authored jump heights, boots, ladders, grapple, water,
moving brushes, teleports, death/respawn and map progression. Native playback
remains available for programs that lack a matching command/replay contract.
Unknown forces must not be erased or guessed by client prediction. A supported
session must survive its later ability/state changes, not merely its dry spawn.

References: exact installed q30a1024 for the first complete AD-family contract;
installed Mjolnir and other AD-derived programs for subsequent qualification;
QSS-M for generic PMove and cooperative QC input/builtin ABI; vkQuake's native
QC/collision/world owners for public desktop and unaware-mod behavior. Inherited
OpenVR supplies VR contacts, roomscale and weapon presentation semantics. Existing
shared weapon/muzzle offsets remain the only calibration; movement ownership
must not add multiplayer offsets.

## Evidence and unknowns

| Fact | Evidence and consequence |
| --- | --- |
| q30 already has a narrow dormant handoff | Verified source: `SV_PrivateWalkTrialQ30Program` pins loader-cached SHA-256; selected handoff retains QC-authored jump velocity/release state and shared PMove `qc_jump_owner`. Admission still requires stock identity. Reuse this code; a size/hash match alone does not qualify gameplay. |
| Partial exact-binary evidence exists | Recorded executed evidence in [mod movement review](migration-mod-movement-review.md), `tests/q30_movement_native_fixture.c`: low/released/held jumps, staged boots/ladder, paired commands, maintenance, support correction and timing survey. Selection is injected in that driver; it is not ordinary admission. Real ladder/grapple/wet transitions and complete snapshot/replay are not proved. |
| QC contains cadence-sensitive branches, not a proved ordinary-dry incompatibility | Exact installed bytecode at54381–54386 scales ladder velocity by0.9 without frametime, but clears `onladder` at54332–54333 first. Repeating that branch depends on intervening state/contact. Selected Pre/PostThink can run per command and on zero-time maintenance; native callbacks run per world frame. The shared Think window only resolves scheduled Think repetition. Qualify actual branch effects before changing ordinary callback placement. |
| Stock corrections and freeze are program-specific | Verified source: stock water/jump reconciliation and exact-stock living intermission predicate. q30 authored forces and later NONE/finale/custom states require their own proved ownership. Do not simply OR q30 into stock admission and discover these cases after selection. |
| Installed q30 has no cooperative command hook | Installed-binary function table reverified: no `SV_RunClientCommand` and no `customphysics` field. Hook declaration and `PR_GetSetInputs` input bridge already exist in 2.0, but server hook invocation and `PF_sv_pmove` are absent. Reuse that existing ABI bridge plus QSS-M builtin semantics, rather than implement a second input bridge. This does not resolve unaware q30 by itself. |
| Native customphysics and stats already work | Verified source: `SV_RunCustomPhysics` dispatches through the existing native owner. Custom-stat admission checks actual overlap; string key stats use existing transport. Preserve these owners rather than adding parallel callbacks or stat policy. |
| Ownership metadata and fresh native input exist | Reverified current source: private queue, completed-command cursor, authority/replay permission, discontinuity epochs and contact/Gorilla invalidation. `SV_Physics_ClientSelectedNativeFrame` now calls `SV_ClientThink` with `private_move_native_frame=true`, so the older missing-input objection is superseded for fresh frame dispatch. Reuse is materially smaller now. Mid-callback terminal/frozen continuation still does not prove a living q30 fallback or full action/clock/replay contract. |

Unknowns to resolve before admission: every reachable q30 movement-state branch,
the minimum unaware-QC callback contract, safe behavior when live abilities
temporarily lack a replay contract, required snapshot seeds for ability replay,
and exact movement correspondence among installed AD/Mjolnir variants. Existing
decompiled/source-like q30 files are useful references, not an exact source build.
Verify the installed bytecode for decisions that depend on branch predicates.

## Adapter comparison and architecture decision

Preferred direction: extend the existing command owner at the QC-to-PMove and
permission boundaries, retaining generic QSS-M PMove and vkQuake collision/QC.
Keep authored jump/ability velocity in its current owner, use existing native
states where a complete phase/input contract proves them reusable, and grant
replay only when the snapshot contains its required state. Determine callback
cadence before changing admission; preserving authored effects has priority over
making a trial selected bit turn on.

For a genuinely cooperative QC program, port QSS-M's input parameter/builtin
logic through the existing registry and world dispatch. Do not transplant its
receipt-time dispatcher: executing movement both at receipt and in the existing
world queue duplicates callbacks, Think opportunity and completion.

Rejected without new evidence: a second continually living native queued solver,
generic subtraction/re-addition inferred from a final velocity delta, blanket
replay for native snapshots, or replay through unavailable server QC. Existing
reviews identified incomplete state/clock contracts in a naive living fallback;
the later fresh-frame adapter has since supplied native input acceleration.
Reassess against that actual current owner, not the stale missing-input finding.
Reopen the architecture decision only if the smallest
adapter cannot express demonstrated behavior; document the duplication and
smallest vertical proof before any rewrite.

## Astra disposition, before production changes

Local Astra reviewed the [verified brief](predictive-mod-admission-astra-brief.md)
with effective `gpt-6-astra` / `max` settings. Main independently redecoded the
pinned program's ladder clear/damping, teleport hold, weapon cooldown and
waterjump writes, and checked the current native input and shared solver paths.
This is a design review, not implementation qualification.

| Recommendation | Disposition |
| --- | --- |
| Prefer fork B: existing per-command QC for qualified ordinary dry movement. | **Adapted.** Keep the command owner and authored QC velocity. Do not reconstruct boots/grapple/ladder forces. Admit only after the bounded dry comparison and matched client replay; use existing fresh native frames for states lacking that contract. |
| Do not introduce fork A's once-world QC wrapper without an ordinary-dry mismatch. | **Adopted.** Ladder damping alone does not prove repeated damping because QC clears its latch; `W_WeaponFrame` checks `attack_finished` at70999–71002. Input impulses still precede that guard at70998 and require effect/completion checks. A demonstrated mismatch reopens this decision before adding a wrapper. |
| Share classification across receipt, physics and snapshots. | **Adopted.** Supported mod states must not disconnect a selected session merely because they need native movement. Resolve typed names against the loaded VM rather than embedding q30 field/global slots. Non-finite or invalid state remains invalid. |
| Choose native frames before QC for incompatible states. | **Adopted.** Reuse coalesced inputs, native `SV_ClientThink`, QC, contacts and completion. Native time does not accrue selected command credit. Classify startup, wet movement, boots, ladder, grapple eligibility, authored holds and camera/lifecycle states before callbacks, including states capable of starting an ability in PreThink. |
| Treat phase transitions according to movement already consumed. | **Adopted.** After movement/contact enters a native state, finish the current PostThink/completion once, stop the batch, and leave later queue heads for the next fresh native frame. Before movement, only an explicitly proved continuation is permitted; no rerun of PreThink, late native acceleration or extra whole-world movement after command movement. Reachable unhandled transitions block admission. |
| Ordinary replay must ship with ordinary admission. | **Adopted.** Dynamic jump height alone is insufficient. Match release/held state, low takeoff support, landing/gravity, QC-authored velocity and callback/discontinuity effects using existing stats/authority epochs. The server's `qc_jump_owner` also changes support geometry; the client currently does not reproduce that policy. Resolve that missing consumer before enabling replay. |
| Keep the stock waterjump adapter away from authored q30 waterjumps. | **Adopted.** q30 writes `FL_WATERJUMP` and its deadline at55290–55304. The current shared callback adapter can clear that authored flag; native liquid ownership avoids the stock correction. Guard/transition correctness must be established before broadening admission. |
| Implement cooperative QC separately through existing QSS-M ABI owners. | **Adopted.** Reuse `PR_GetSetInputs`, builtin registry, world dispatch, physent collection and PMove. q30 has no cooperative hook and is not the consumer proof for that feature. Its later bounded plan must identify an actual hook/builtin program. |

No user taste or priority decision is needed for these boundaries. Remaining
unknowns concern code behavior and replay consumers and stay implementation work.

## State and phase contract

| State / phase | Input and movement owner | QC clock/effects | Completion and replay |
| --- | --- | --- | --- |
| Qualified ordinary dry q30 WALK, fresh command | Existing selected queue and shared PMove; QC retains its authored jump impulse/release. | Pre/PostThink use command duration. Existing shared Think window grants one scheduled world opportunity. Confirm ordinary weapon/impulse effects in the exact binary comparison. | Complete after the existing lifecycle. Grant replay only after a matching ordinary client contract is implemented and checked. |
| Quiet ordinary dry WALK or insufficient credit | Existing selected maintenance; no new movement duration or input sequence. | Existing zero-duration maintenance plus the one world Think opportunity. Prove quiet effects are compatible rather than assuming zero frametime makes all QC inert. | No command ACK or fabricated duration. |
| Incompatible mod state detected before callbacks | Existing selected-native fresh frame: coalesced levels/latches/roomscale, then native input and physics. | Native world-duration QC/physics and ordered contact drain. Preserve authored boots, ladder, grapple, waterjump, holds and camera behavior. | Existing native completion/retirement, no replay, selected credit cleared. Existing authority/mode epoch handles reentry. |
| Terminal/frozen transition before movement | Existing proven native phase continuation only for its qualified states. | Execute remaining phases once with restored clocks; never restart PreThink. | Existing completion; native authority and no replay. Living q30 transitions need explicit proof before using this boundary. |
| Native transition after PMove/impact/contact | Keep already-consumed command movement; finish the current lifecycle once and stop batching. | PostThink/contact effects remain in their current owners. Do not append another native movement interval. | Complete current head once; preserve later heads for fresh native dispatch next world pass. Suppress replay for the transition. |
| Return from native to qualified dry WALK | Existing selected owner after fresh current-state classification. | Rebuild current movevars and use current QC state; do not reuse stale native credit or support. | Existing mode/discontinuity epoch and a complete valid replay seed; no blanket permission from WALK alone. |

The table defines an intended contract, not proof that every row is implemented.
In particular, today's stock-only phase helpers do not yet qualify living q30
holds/cameras. The ordinary replay consumer exists, while production q30
selection and server prediction permission remain closed. The
[native-state/admission plan](predictive-q30-transitions-2.0-plan.md) defines
the remaining implementation and normal-session acceptance.

## Next bounded slice and exact ownership

Before admission, extend the existing exact-q30 fixture to discriminate the
ordinary-dry per-command hypothesis from actual cadence or transition failures.
This is a decision check allowed by the user's exception for uncertain changes;
it is not a performance benchmark or a test after each edit.

Write set for that slice: `tests/q30_movement_native_fixture.c`, its existing
build owner `tests/customphysics_native.make`, `tests/README.md`, and this plan's evidence
checkpoint. Reuse the current native engine bootstrap, actual q30 bytecode,
player/world hulls, command queue and retirement; do not add a transport or
replacement QC scheduler. Cover ordinary dry paired press/release, held/rejump,
quiet maintenance and command batches, observing velocity/origin, release flags,
weapon/impulse effects and completed sequences. Separate real QC transitions
from fields deliberately staged at a component boundary. Record tolerances and
native-reference differences rather than silently normalizing the reference.
If additional map resources or an unhandled transition are necessary, record
the evidence and refine this write set before adding a new harness.

Production write ownership for the following coherent admission/replay slice:
`sv_phys.c` for QC/solver and phase boundaries; `sv_main.c` for program admission,
stats and snapshot permission; `sv_user.c` for receipt agreement; `pmove.c` /
`pmove.h` for a demonstrated missing shared solver input; `cl_main.c` for its
existing replay consumer. Do not change the protocol or another subsystem under
this write set. A new replay policy bit or schema requires a separately recorded
consumer/compatibility decision before code. Ordinary admission and matched
replay are integrated together, followed by native-to-predictive return proof;
another dormant q30 helper is not the completed slice.

Main owns design/integration. Any coding worker receives a subset of the exact
write set, a concrete behavior contract and non-overlap instructions. No agent
may broaden scope to a new owner or change the user-dirty migration document.

## First bounded comparison checkpoint

Implemented in the existing q30 native fixture after plan commit `355fa8cd`;
Linux SDL3/-Werror linkage and the consolidated ordinary run exit0 with all old
and new markers. This is component-chain decision evidence, with injected
selection/input and prepared QC clock; no ordinary q30 admission or client
replay is implemented by the fixture.

| Executed case | Result / limit |
| --- | --- |
| Initialized dry hold, actual landing, release and re-jump | Forty-eight 8ms commands execute real q30 QC and map hulls. Native and selected take off twice and match velocity/release/ground flags each command. Peak position difference0.659554, landing reconverges. One-unit local survey bound; no identical integrator or general map-traversal claim. Four native warm-up commands exclude startup from this comparison. |
| Two/eight 5ms command attack batches versus a native single-world frame | Actual shotgun QC/impulse2 spends one shell in both owners; shells, weapon, currentammo, consumed impulse and cooldown match. Native comparison uses the existing coalesced adapter and the same world duration, not eight falsely equivalent native world callbacks. |
| Quiet maintenance and impulse | Before cooldown, no extra shell; after the prepared clock crosses the authored deadline, held attack spends another shell without command duration/ACK. Impulse1 selects axe once and maintenance keeps that selection. Zero-time callbacks have required gameplay effects. |
| Authored hold under fresh native ownership | Staged `pausetime` activates actual QC: two forward commands finish with no movement/credit, then two later commands move after expiry. Direct adapter call/clock staging, not a real teleporter or native-to-predictive return test. |
| Cooldown controls | Exact binary independently has returns in `W_WeaponFrame` at70999–71002 and `W_FireShotgun` at69399–69402. Test-only bypass of the frame guard still exits0; bypass of both guards fails the first selected batch's one-shell assertion (exit134). The extra weapon guard was discovered because the first negative-control hypothesis failed. Assets/production code remain unchanged. |

The dry results do not demonstrate a need for a once-world QC wrapper. Keep
the reviewed command owner; quiet QC effects remain owned by maintenance.
Ordinary replay still needs the matching pre-solver jump/support/latch consumer
and compatibility gate described in the
[next verified brief](predictive-q30-replay-astra-brief.md). Ability/real-trigger
transitions, shared production classification, full snapshot/replay,
native-to-predictive return and actual begin admission remain required before
this major feature is complete. These checks are decision evidence for the
user's uncertain-change exception, not repeated tests of unchanged features.

## Stages and ownership

1. **Resolve complete first-program ownership.** Main prepares a bounded verified
   brief from the current owners and installed q30 binary. Astra challenges
   callback cadence, ability transitions and simplification opportunities.
   Produce a state/phase table: input/QC/solver owner, clock, callback effects,
   completion and replay permission. Include quiet/paired/batched commands and
   mid-callback state changes. This table is the prerequisite to admission,
   not a request for a new protocol/state machine.
2. **Implement the narrow resolved boundary.** Production candidates are
   `sv_phys.c` QC-to-PMove/native phase boundary; `sv_main.c` admission/stat/
   permission owner; existing `pmove.c` inputs only if the solver lacks a proved
   necessary input. Receipt validation in `sv_user.c` must agree with actual
   current state. Keep collision, VM lifetime, queue retirement and callback
   clock restoration. Record exact write ownership after the design table is
   resolved; this plan does not authorize an unspecified broad refactor.
3. **Reach ordinary q30 gameplay.** Extend existing real-QC/bootstrap drivers to
   actual offer/spawn/begin, complete commands, snapshot parser and client replay.
   Require the complete first-program state contract before admitting a normal
   session. Dry/jump proof alone must not create a session that later disconnects
   when it reaches water, an ability, intermission or native movement state.
4. **Complete replay where state is available.** Determine the smallest seed and
   existing movevar/stat consumer for each supported force/ability. Withhold
   permission only where the client cannot reproduce the authoritative contract;
   keep command consumption and visible gameplay working. New wire data needs
   a demonstrated missing consumer and its own bounded review. No duplicate
   ability state machine simply to conceal unavailable QC.
5. **Expand to AD/Mjolnir and cooperative programs.** Verify actual installed
   identities/branch correspondence and reuse the same boundary when semantics
   match. Add only evidenced deviations. Cooperative `SV_RunClientCommand` /
   `runstandardplayerphysics` support gets a bounded QSS-M ABI reuse plan before
   its implementation; hook-free q30 is not a consumer proof for that feature.

Expected complexity: a focused boundary adapter plus existing fixture extension
after the ownership table is resolved. A callback scheduler, new physics owner,
new protocol or repeated interaction fixes exceed this estimate and reopen the
architecture. Delegate implementation to the user-requested coding model with
one owner per coupled region; keep analysis, final integration and judgment in
main/Astra. If that route is unavailable, report it rather than silently changing
models. Each worker has a precise write set and must preserve others' edits.

## Consolidated acceptance and remaining scope

After the coherent feature is implemented, compare real QC effects, origin,
velocity, flags, timers, ability state and completion against native reference
under stated tolerances. Use actual trigger/pickup/Think branches where claiming
them, not merely staged ability fields. Cover ordinary and low jump/release/
re-jump, boots, ladder, grapple, wet/ledge, quiet and command batches, pusher
carry/rollback, causal pause/recovery, teleport, death/respawn, map progression
and load/initial native boundaries. Test generated private desktop/VR and public
desktop peers in the same actual world. Exercise full snapshots and replay
separately from authoritative correctness; check rejection of malformed input
without treating supported gameplay as malformed.

Run final Linux linkage and focused checks appropriate to changed owners once
the slice is coherent, then local Astra review and disposition. Do not retest
unchanged modules after every edit. Captured delivery/prepared resources are
explicit component seams; do not invent a network simulator to bypass this
sandbox. Actual device/eye testing, connected live playtesting, Windows/ARM
qualification and performance measurement remain deferred by the user. Those
limits do not excuse missing mod implementation. The full migration remains
active until its implementation scope is finished.
