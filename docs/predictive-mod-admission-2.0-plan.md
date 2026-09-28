# AD-family predictive movement: admission and ownership plan

Status: next major-feature plan, before new production edits. The stock default
activation slice does not admit q30 or finish mod prediction. This plan expands
stages 3–4 of the [parent movement plan](predictive-movement-2.0-plan.md).
An Astra design disposition must resolve the command/callback contract before
the first production admission change; unresolved questions below are not
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
| QC cadence is a demonstrated incompatibility | Recorded exact installed bytecode at54381–54386 scales ladder velocity by0.9 without frametime. Selected Pre/PostThink can run per command and on zero-time maintenance; native callbacks run per world frame. The existing shared world Think window only resolves scheduled Think repetition. Repeating unaware QC is not automatically safe. |
| Stock corrections and freeze are program-specific | Verified source: stock water/jump reconciliation and exact-stock living intermission predicate. q30 authored forces and later NONE/finale/custom states require their own proved ownership. Do not simply OR q30 into stock admission and discover these cases after selection. |
| Installed q30 has no cooperative command hook | Recorded installed-binary function-table evidence: no `SV_RunClientCommand`. Verified current branch: hook declaration exists, but there is no invocation or `PF_sv_pmove`. The QSS-M hook/builtin is reusable for programs that actually supply the contract; it does not resolve unaware q30 by itself. |
| Native customphysics and stats already work | Verified source: `SV_RunCustomPhysics` dispatches through the existing native owner. Custom-stat admission checks actual overlap; string key stats use existing transport. Preserve these owners rather than adding parallel callbacks or stat policy. |
| Ownership metadata exists | Verified source: private queue, completed-command cursor, authority/replay permission, discontinuity epochs and contact/Gorilla invalidation. Native terminal/stock-frozen continuation is phase-aware, but it is not a proved living q30 fallback: selected clients skip ordinary native input acceleration. |

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
reviews identify missing native input acceleration and incomplete state/clock
contracts in a naive living fallback. Reopen that decision only if the smallest
adapter cannot express demonstrated behavior; document the duplication and
smallest vertical proof before any rewrite.

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
