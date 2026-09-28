# Ordinary stock predictive-movement activation

Status: preimplementation plan, baseline `66fdea42` on `2.0`. This advances
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
Desktop/local-SP and loaded-world native behavior remains available. The user
has not required network-style local-SP physics; mod compatibility and modern
mod prediction still require the parent stages.

## Verified current state and unknowns

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

## Astra design disposition

Pending bounded review before production edits. Main's lean is to activate the
already qualified stock owner without inventing another lifetime or weakening
validity checks. Any remaining expensive fork is reviewed at its actual owner.
