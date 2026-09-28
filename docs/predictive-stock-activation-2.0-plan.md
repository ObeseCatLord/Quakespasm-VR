# Ordinary stock predictive-movement activation

Status: bounded stock activation implemented; final Linux/focused checks and
local Astra/max implementation review accepted, including the delayed-contact
correction. That correction was planned before its production edit. The plan was committed before production
edits (`63d44ae1`) and reopened for stock intermission (`0543cfd3`), baseline
`66fdea42` on `2.0`. This advances
stage 2 of the [full predictive movement plan](predictive-movement-2.0-plan.md).
It does not replace the required AD-family/cooperative-QC and mixed-gameplay
stages with a stock-only goal.

## Behavior and reference

An ordinary 2.0 desktop or VR peer joining a compatible stock server should
reach the implemented sequenced command/PMove/replay path without a trial
console command. Public peers share that world through vkQuake's native owner.
QSS-M supplies generic prediction; the existing private queue and inherited VR
commands supply completion/pose/contact semantics. vkQuake remains the native
QC, collision, connection and world reference. Prediction remains a per-state
permission, separate from command ownership: native cheat/death states, pause,
moving brushes and raw planted pusher palms already have explicit boundaries.

Preserve explicit server disabling. Unsupported programs and initial states
stay functional through their existing native admission path, with a clear
diagnostic; do not disconnect them merely because production selection is on.
Desktop/single-slot local-SP and loaded-world native behavior remains available.
Multi-slot listen hosts have sockets and are not excluded merely for being local.
The user
has not required network-style local-SP physics; mod compatibility and modern
mod prediction still require the parent stages.

## Verified preimplementation state and unknowns

| Claim | Evidence / implication |
| --- | --- |
| Private transport already defaults on; selected movement defaults off. | [verified source] `sv_main.c` cvars are `sv_qsvr_private=1`, `sv_private_pmove_walk=0`. Ordinary client `ClientOffer` reaches the private marker; transport alone is not movement selection. |
| One existing begin boundary selects ownership. | [verified source] `Host_Begin_f` calls `SV_PrivateWalkTrialSelectAtBegin` before setting spawned. Its guard prevents mid-session/repeated selection; failed admission returns to existing native play. |
| Current admission has bounded program/state guards. | [verified source] `SV_PrivateWalkTrialAdmissionFailure` requires matching private profile, exact stock QC, disjoint stats, remote multi-slot non-loadgame, initial dry WALK/SLIDEBOX, no customphysics/trusted authored hands, and valid support offsets. Already-selected snapshots use current frame validation. |
| Selected execution already covers the important stock transitions. | [verified source and prior software evidence] Shared PMove wet/ledge and raw Gorilla, native NOCLIP/FLY/death/respawn continuation, arrival-gap/causal pause recovery, teleport fences and robust native push/carry/rollback. Linked parent slices describe actual-code checks and their prepared/captured seams. |
| Permission has independent current-state exclusions. | [verified source] `SVFTE_WriteEntitiesToClient` separates selection/authority/permission; pause, native dispatch, QC holds, body pusher marks and retained pusher palms do not imply replay permission. |
| Current component bootstraps depend on default-off selection. | [verified source] `mixed_native_fixture.c` asserts off and sets on only for `-selected`; liquid bootstrap likewise does not explicitly disable native reference runs. Negotiation fixture checks untouched0. Those test policy assumptions must change with the product default; selection bits/ACKs must still never be injected as admission proof. |
| Private demos have their existing self-describing framing. | [verified source/prior checks] Stage0's writer/protocol reader and entity decoder preserve private layout during playback without live prediction. Keep that code; default activation needs no demo dialect. |

Unknown: natural connected mixed play and physical device input in this session;
unqualified stock transitions outside the actual implemented state contract;
initial wet/load/local automatic PMove; arbitrary mod/client equivalence. The
current sandbox blocks sockets and the user defers live device/Windows/ARM/
performance qualification. These limits do not justify a test transport or
leaving already implemented supported gameplay behind a trial default forever.

## Minimal adapter versus replacement

Preferred: change the existing server selection default to1 after source review
of its admission/transition boundaries; update the two admission diagnostics
to describe ordinary stock predictive movement. Preserve every existing
program/state/finite/framing guard, initial native admission, queue, callback,
world clock, completion and snapshot owner. Keep internal symbols/cvar name
for now; wholesale renaming adds unrelated churn without user benefit.

Rejected: a new client/server activation negotiation duplicates the already
matched profile; a new fallback simulator duplicates native dispatch. Blanket
mod admission or public/native replay would claim a matching solver contract
without evidence. Do not broaden admission during a partially executed command
or restart QC callbacks. Initial wet/load/local expansion belongs to a stated
follow-up decision if it is needed, not an incidental removal of guards.

## Stages, ownership and acceptance

1. **Review the default boundary.** Local Astra audits verified source and the
   prior stock slices for demonstrated production-default blockers. Rank any
   valid-state disconnect or duplicated policy higher than labels. Main checks
   findings and records disposition before production changes. Reopen this plan
   if a new living movement owner or broad admission change is required.
2. **Activate existing supported stock behavior.** Main owns only cvar default
   and the two begin/admission diagnostics in `Quake/sv_main.c`, plus a stale
   descriptive comment in `sv_phys.c` if needed. Other movement/protocol owners
   stay intact unless review demonstrates an exact incompatibility.
3. **Make fixture policy explicit and exercise untouched defaults.** One coding
   worker may own `tests/negotiation_native_fixture.c`,
   `tests/mixed_native_fixture.c`, `tests/stock_liquid_native_fixture.c` only.
   `-defaultselection` must observe untouched production defaults and invoke
   actual offers/spawn/begin/commands; `-selected` retains explicit setting and
   native comparison cases explicitly set0. Assertions compare actual selected
   ownership, complete snapshots, gameplay and replay rather than injecting it.
   Main owns integration/docs and any distinct focused admission driver. The
   worker is not alone, must preserve others' work and report missing evidence
   instead of broadening its write set. Use the authorized single web coding
   route if Luna is unavailable; do not silently substitute another local model.
4. **Consolidated software check.** Untouched-default negotiation and default
   mixed private-VR/public-desktop actual QC movement/fire/replay/mode/recovery
   chain; explicit-disabled/native counterpart; default wet/ledge/causal-pause
   and planted pusher carry/invalidation chains reuse existing drivers. Final
   Linux `-Werror` build and meaningful focused sanitizers after the coherent
   change, then final bounded Astra review. Previously qualified unchanged
   decoder/body/brush cases need no broad speculative rerun.
5. **Record usable behavior and remaining scope.** Update README/probes that
   assumed ordinary private permission off: explicit-native diagnostics must
   request server disable, while default stock probes expect selected movement.
   Describe initial excluded states as native compatibility, not malformed input
   or finished mod prediction. Commit explicit files; never stage the user's
   `docs/migration-2.0.md` edits or change `main`.

Completion of this stage means ordinary compatible stock admission activates
the real implemented owner and local software evidence covers the default
path, with explicit-disable/public isolation retained. It does not mean the
whole migration is done. Next major feature plan must turn the exact installed
AD/q30 and cooperative-QC analysis into admitted gameplay, preserving authored
forces and the existing native owner where client replay lacks a contract.

## Astra design disposition and reopened lifecycle boundary

Local `gpt-6-astra/max` read-only review; main independently verified effective
model/effort fields and its load-bearing source/QC claims. The review found a
real ordinary-stock blocker before the default change, not merely more missing
tests. Main decoded the pinned installed QC's global/field/function definitions
and exact statements: `intermission_running` is a float; `execute_changelevel`
stores `SOLID_NOT`/`MOVETYPE_NONE` at5858/5860, `finale_1` at20878/20880.
Living owners then fail the selected hull/classification at receipt/snapshot.

| Recommendation | Disposition before production edits |
| --- | --- |
| Recognize actual pinned-stock living intermission/finale without broadening WALK. | Adopted. Add a pure frozen-state predicate requiring exact stock program, finite positive typed `intermission_running`, living owner and `SOLID_NOT/MOVETYPE_NONE`. Classify it as the existing native state before the living stock hull guard. Keep PMove's post-callback WALK check strict. |
| Continue only remaining callback phases when freezing happens mid-command. | Adopted. Reuse the existing phase-aware native continuation for PreThink/weapon-Think freezes; do not rerun completed callbacks. After movement/contact/PostThink freeze, preserve completion and mark native authority without another move pass. Existing invalidation/timer cleanup handles the hand baseline. Do not redefine dead/terminal eligibility to include living frozen players. |
| Exclude unsupported elevator modes before ordinary platform contact. | Adopted. Initial stock admission requires robust mode>=3 even on static spawn, so legacy settings remain native from the beginning. This avoids admitting a known future valid-state rejection; selected legacy platform physics is not added. |
| “Remote” admission does not exclude multi-slot listen-host sockets. | Corrected wording. Preserve the existing single-slot/missing-socket guard; no new local exclusion is required by the user or parent plan. |
| Cover actual map progression and finale, not only synthetic hull fields. | Adopted. Reuse the actual admitted component chain with installed exit/Think/IntermissionThink and renewed serverinfo/spawn/begin after actual queued changelevel; include quiet/batched frozen commands. Finale may use prepared actual callback activation and must label it instead of claiming natural boss progression. |

Amended production write set: `sv_main.c` shared stock-identity predicate,
admission/default/diagnostics; its declaration in `server.h`; `sv_phys.c` pure
frozen-state classification and existing phase/publication/dispatch boundaries.
The existing after-weapon continuation retains pre-Think WALK by default: only
the demonstrated selected stock freeze may reselect `MOVETYPE_NONE` there,
otherwise a desktop selected player could integrate once after being frozen.
No new queue, protocol, callback clock, persistent intermission state or native
solver is introduced. Main owns this tightly coupled correction and
`tests/stock_intermission_native_fixture.c`; native admission driver gains an
actual elevator-cvar exclusion. The web worker failed before edits and was
closed; main owns the planned common-fixture policy delta as well.

Smallest vertical proof now includes actual exit-trigger QC, living freeze,
native authority/no replay, command and quiet-frame completion without body
integration, actual button-driven queued next map and renewed default admission,
plus the actual pinned finale callback with explicit activation seam. Compare
native execution and once-only callback/clock obligations; retain negative
unrecognized NONE states. Consolidate default/regression/build/sanitizer checks
after this correction. If these boundaries grow into a new movement phase or
parallel state machine, reopen the architecture instead of extending the patch.

## Implemented checkpoint and consolidated evidence

The supported stock default is now1. Existing actual begin selects the same
queue/PMove/completion owner; matching transport before begin does not imply
selection. Explicit disable0, public peers and initially unsupported states
retain native movement. Initial legacy elevator mode<3 remains native even
on static floor. This setting is not a mid-session revocation mechanism.

Stock living freeze classification observes the typed, bounded actual QC
global and exact program identity; it does not store another intermission
lifetime. PreThink/weapon-Think freezes reuse the existing remaining native
phases and world clock. Late movement/contact/PostThink freezes commit once
without another integration or accepted hand publication. The existing native
loop owns later frozen frames and pending batch retirement. Unrecognized
living NONE remains rejected, and dead/terminal classification is unchanged.

| Completed software check | Evidence and practical bound |
| --- | --- |
| Untouched defaults/negotiation and mixed gameplay | Defaults1 asserted before offers; actual spawn/begin, generated private VR/public desktop movement/fire, complete snapshots/replay, native modes, death/respawn, teleport and early pause/arrival-gap recovery pass. Explicit-native0 comparison also passes. Signon/resources/input and delivery remain prepared/captured component boundaries. |
| Previously implemented stock paths under ordinary defaults | Wet/ledge/causal pause and planted moving-brush/pending replacement chains pass through actual default admission. Existing broad decoder/brush tests are not repeated without a changed owner. |
| Initial native admission | Seven cases pass actual begin, repeated-begin isolation and commands/native gameplay: wet, noclip, disabled, load, program, slots, elevators. Wet is real BSP/water categorization; noclip/elevators use actual cvars/commands. Load/program/slots are prepared admission metadata, not completed save/load, foreign-mod or local-transport qualification. |
| Actual exit and renewed selection | Real e1m1 trigger/installed Think, reliable intermission fanout/parser, native frozen quiet/batch clocks/ACKs, deadline/button-driven IntermissionThink/NextLevel and actual host changelevel to e1m2 pass; renewed serverinfo/spawn/begin selects again. Native0 comparison passes. Reliable reconnect broadcast is captured/asserted, client resources prepared; no connected reconnect routing is claimed. |
| Finale and freeze phases | Actual end-map boss/train with prepared installed death callback passes. Prepared composition invokes actual stock exit QC after real PreThink/due Think/PostThink; quiet and three-command cases verify once-only callbacks, queue-head completion, remaining tail retirement and no post-freeze body integration. No natural boss combat/scene-rendering claim. |
| Final build and focused sanitizers | SDL3 Linux `-Werror` build passes. Final partial ASan/UBSan driver passes nine cases: default/native exit, prepared finale and the three callback phases with both quiet/batch input. Included physics/server/client owners instrumented; remaining engine objects normal, leaks disabled. No whole-engine sanitizer claim. GDB probe's embedded Python parses, but ptrace execution is unavailable. |

The [verified final review brief](predictive-stock-activation-implementation-astra-brief.md)
records exact owner lines, local evidence paths and the explicit seams. This
bounded stage does not complete the parent goal: AD/q30/Mjolnir admission,
cooperative QC hooks and wider local/load/mixed compatibility need their next
preimplementation plan. Device/eye/platform/performance trials remain deferred
by the user. No release transport or alternate test server was introduced.

## Final Astra finding: delayed contact correction plan

Local `gpt-6-astra/max` final source review found a conditional regression,
independently checked by main: selected receipt retains frozen commands, native
completion drains their physical contacts, and the existing living/fresh/weapon
eligibility does not reject living NONE/NOT. Installed exit/finale retains the
weapon identity. Delayed valid axe samples can establish continuity, accumulate
a stroke and emit a physical whiff sound/cooldown after freeze; explicit native0
discards those arrivals. Earlier generated VR commands had no contact records,
so their passing frozen checks did not cover this behavior.

| Recommendation | Disposition before correction |
| --- | --- |
| Reuse the existing exact frozen predicate at contact eligibility | Adopted. `sv_phys.c:SV_VRContactSampleValid` rejects selected exact-stock frozen contact execution; a forward declaration shares the same predicate. Existing drain still advances its cursor and clears contact/stroke continuity. Public/native and living stock contact policy remain unchanged. |
| Prove delayed valid physical contacts through actual input/receipt | Adopted. Extend only the existing intermission driver with an optional prepared axe selection and encoded contact sequence delivered after actual freeze, before reliable client intermission parsing. Compare default-selected/native0; assert no contact QC cues/cooldown/hostility effects, while callback counts, command retirement and map progression still complete. Include an ordinary live validity/positive control so an invalid sample cannot make the negative test pass. |
| Replace or generalize native continuation | Rejected. The existing phase adapter is necessary and correct; this incompatibility is at contact eligibility, not a missing movement or transport owner. |

Production write set remains the existing `sv_phys.c` owner; test write set is
`tests/stock_intermission_native_fixture.c`. No general intermission contact
service or another phase is introduced. After this coherent correction, rebuild
Linux and the affected fixture, run delayed-contact default/native and frozen
phase/finale cases with focused sanitizers as warranted, then request bounded
follow-up Astra disposition. Do not reopen unchanged mixed/decoder/map tests.

Correction evidence: actual impulse1/live encoded axe contacts produce a real
QC whiff as positive control. The same delayed sequence after actual freeze,
before reliable client notification, passes default/native0/finale and all
PreThink/Think/PostThink quiet/batch compositions. The nine updated partial
ASan/UBSan cases pass; final ordinary Linux `-Werror` linkage also passes.
An isolated source copy without only the eligibility guard reproduces cue
delta1 and cooldown/hostility delta0.775, failing the original no-effects
assertion. This upgrades the review's conditional source finding to a reproduced
component regression and its correction; no alternate transport is added.
Final bounded follow-up Astra disposition: **accepted**, local `gpt-6-astra/max`,
effective fields independently checked. Main spot-checked the eligibility/drain
cursor order, actual-code control differences and output markers. The earlier
stock movement/default disposition stands; no further software gate is needed
for this contact correction.

| Final recommendation | Main disposition |
| --- | --- |
| Keep the exact-state adapter/default; resolve only delayed contact eligibility | Adopted. The shared predicate and existing drain close the reproduced path without a second movement/contact lifetime. Public/native and live stock behavior are retained. |
| Require meaningful delayed contact evidence rather than pose-only frozen commands | Adopted. Actual live cue/cooldown positive control, delayed default/native comparison, guard-removal failure and nine affected sanitizer cases supply the bounded behavior proof. |
| Preserve documented limits instead of expanding this correction into parent mod work | Adopted. Captured delivery, prepared resources/finale activation and partial instrumentation remain explicit. Broader implementation proceeds under the separately committed [AD-family admission plan](predictive-mod-admission-2.0-plan.md). |
