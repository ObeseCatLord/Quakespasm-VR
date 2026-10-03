# Co-op save identity/reference boundary — verified senior-review brief

2026-10-03. Existing frozen V06/F02 owner; no additional feature inventory.
Production079f4431. User excludes unavailable tests, but the following native
software case is attainable and fails. Main keeps production unchanged while
reviewing whether the assertion and proposed correction are necessary.

## Verified evidence

Main read `host_cmd.c`2489–2582,3180–3230,3645–3730 and the new opt-in
`local_load_native_fixture.c`529–814. Native stock e1m3/QC runs real pickups,
T_Damage, v7 writer/reader, player snapshots, name match and reverse reconnect.
Beta originally occupies edict2; the written save includes `world.enemy=2`.
Beta rejoins slot1 with its own living inventory; dead alpha rejoins slot2 with
its own restored inventory. The final world reference points at live alpha2,
not beta1. Native assertion aborts. Main checks log at
`FastGames/qsvr-protected-native-72o89z1i/profiles/qsvr-local-load-coop-lifecycle-d97i5l26/native-run.log`:
five preceding co-op markers pass; exact reference owner is beta1 versus actual2.
Prepared beta endpoint is a fixture seam, not two actual sockets. The existing
nine native local/load cases and autosave acceptance remain credited.

`Host_LoadgameSaveClientEdict` snapshots only QC payload plus saved alpha. At
inherited load, saved player edicts are copied then ED_Free'd. Named spawn copies
the saved payload into the current connection's reserved edict, calls native
link/shared-inventory helpers, and clears the snapshot. No identity-based entity
reference relocation is performed. Ordinary client edict number remains tied to
connection slot. The readonly main donor uses whole-edict memcpy restore and
also has no apparent reference relocation; copying that raw metadata is unsafe
and would not solve this boundary. QSS-M provides native save/reference owners
but not this inherited named multiplayer restore policy. No existing relocation
helper was found in target/QSS-M's Quake tree by exact symbol search.

## Design constraints and candidate assessment

Keep native v5/default vkQuake desktop behavior, protocol and live client slot/
edict correspondence. Keep existing save headers/name matching/snapshot owner
and payload-only restoration. Avoid a second save service or new wire layer.
Never remap integer/float/string/function values as if they were typed entities.
Never copy area links, retain counts or other live edict metadata. No per-frame
scan or new persistent gameplay state machine.

A naive each-spawn substitution saved2→live1 cannot distinguish references to
still-pending saved alpha1 from newly rebound beta1; a later1→2 substitution
would corrupt beta references. Deferring one permutation until all reconnect
also cannot distinguish newly generated live references during intervening play.
Changing the live client slot/edict relation could avoid that ambiguity but would
risk native transport/snapshot/colormap/ownership expectations. A bounded
load-time typed-reference staging adapter might avoid ambiguity, but must prove
native allocator/lifetime/pending-discard/serialization behavior and reuse
existing snapshot/edict helpers rather than creating a parallel engine.
These are hypotheses for review, not permission to implement a broad rewrite.

## Open decisions for local Astra xhigh

1. Independently verify the failure and whether the final assertion represents
   required V06 behavior rather than an unsupported synthetic expectation.
   Challenge the premise using actual save/reference and donor semantics.
2. If correction is necessary, choose the narrowest safe boundary; compare
   minimal native adapters with slot or save-system rewrites. Specify the exact
   existing owners that can be reused, duplicated state/policy avoided, and
   expected change size. Reopen architecture if it needs a new state machine.
3. Specify typed live globals/edict fields/pending snapshot coverage, collisions,
   dead/living/pending/new-player/discard handling, and ordinary v5 isolation.
4. Give a concrete smallest end-to-end verification requirement and whether
   final source refresh affects all four shipping artifacts. Do not expand the
   frozen checklist or require unavailable authored KEX/hub assets/hardware.

Return read-only review, max1400words, claim/evidence table, ranked decision,
recommended implementation boundary or justified disposition, required tests,
and residual risks. Do not edit files or repeat a185-feature audit. Main will
spot-check and record disposition before any production implementation.


## Main senior disposition and concrete implementation plan

Local Astra/xhigh Kant independently verifies the v7 file, native failure and
source/donor paths. The ordinary typed reference oracle is required V06 behavior.
This review approves the design boundary, not implementation or final signoff.

| Recommendation | Main disposition and verified basis |
| --- | --- |
| Fix references while preserving existing named payload restore. | Adopt. Main reads target snapshot/free/spawn owners and the saved beta2→live1 failure. |
| Sequential or delayed reference permutation is ambiguous. | Adopt. First spawn resumes the world; saved alpha1 and new beta1 would otherwise share an offset. |
| Use inert pending identity anchors with native lifetime helpers. | Adopt narrowly: only referenced identities, one snapshot-associated offset array, typed fields/globals/snapshots; no active proxy players or per-frame work. |
| Correct the intermediate test expectation of still-freed beta2. | Adopt. The repaired boundary must resolve beta immediately, then keep still-pending alpha distinct. |
| Fresh native allocation must avoid saved free/forward-reference targets. | Adopt. Main reads ED_ParseEpair1400–1448: references can expose targets past the final body; load shrinks num_edicts again. Native ED_Alloc77–121 already has a complete fresh-tail branch. |
| Reopen a tiny allocator write boundary if necessary. | Main explicitly expands the write set to pr_edict.c/progs.h: extract/reuse the existing fresh-tail branch as ED_AllocFresh; normal ED_Alloc retains the FIFO reuse branch and calls that helper for its previous tail case. Do not duplicate allocator internals in host_cmd.c. |
| Ordinary v5/KEX and slot/transport policy stay native. | Adopt. Only inherited multiplayer pending restoration enters the adapter; anonymous matching stays existing policy. |
| Verify collisions, allocator lifetime, typed coverage and actual QC consumption. | Adopt in the existing opt-in native case and affected default/load guards. Preserve earlier positive markers and precise endpoint limits. |

Implementation ownership: `Quake/host_cmd.c`, `Quake/server.h`,
`Quake/pr_edict.c`, `Quake/progs.h`, and the existing opt-in
`tests/local_load_native_fixture.c`/`tests/run_local_load_native.py` only.
Main owns documentation/integration and reviews the complete diff. Native
fresh-tail code is reused, not a second allocator or temporary FIFO manipulation.
The helper keeps baseline/debug initialization and the original allocation hook.
A load-time typed scan finds referenced saved players and the highest admitted
entity offset across native global definitions/current QC payloads and pending
snapshot payloads. Preflight space for fresh indices strictly beyond both final
body and that highest reference before any relocation. If gaps must be exposed,
use the native fresh-tail initialization/free helpers, then retain each selected
anchor before freeing it. Store only its offset alongside the existing snapshot.
Use existing byte offsets in `qcvm->entityfieldofs`; do not scan arbitrary words.
Resolve the matching offset after successful living/dead restore, before snapshot
consumption/publication, then release its retained free edict. Resolve any
existing in-world pending-discard path to world before release; whole VM/map
teardown remains the existing owner. Do not add a discard command.

Expected120–180 production lines plus the small extracted helper; reopen before
approximately220 production lines, parallel lifetime policy, callback/connection
admission changes, active proxies or interpreter interception. No implementation
beyond those limits is authorized by this plan without a main architecture
reassessment. Tests run after the complete bounded edit, and any production
change refreshes affected source/engine artifacts on all four platforms. Earlier
unaffected rendering/audio/input proof retains its exact documented scope.


## Main implementation recheck brief, 2026-10-03

[verified: main diff/direct source reads] Final worker patch is120added/1deleted
production lines in host_cmd.c/server.h/pr_edict.c/progs.h. The prior design's
native typed-reference adapter is implemented, not a new save/protocol/slot policy.
Main verifies native ED_Alloc FIFO behavior remains unchanged and its existing
fresh-tail initialization/debug fields/hook move into ED_AllocFresh. Stage scans
only entityfieldofs/live payloads/pending payloads/ev_entity globals; preflight
covers final body and highest admitted forward target. Native rebuild/allocate/
retain/free/release remain the owner. Anchor offsets are now obtained while live:
main caught the intermediate EDICT_TO_PROG-after-free mistake and worker corrected
it before final fixtures. General Debug conversion checks are unchanged.

[verified: main current hashes and private receipts] All six source/test hashes in
FastGames/qsvr-v06-reference-km59fzty/receipts/final-manifest.json match workspace;
both private binary hashes and all12case log hashes/markers match. Worker rebuilt
all225objects under -D_DEBUG into separate outputs, bypassing original PCH. The
original shipping/native225 binary remains unchanged936635e2...b3fba8. Main reads
final test diff: original five positive co-op markers/strict reference predicate
retained; corrected intermediate expects immediate living beta1 and distinct
pending alpha anchor. Actual loaded QC T_Damage through world.enemy now affects
only beta. Pending typed world/global/payload/self/cross references, intervening
frames/new beta references, newcomer/drop, native allocator/rebuild, referenced
free/forward target, equal float/vector/function controls, existing pending-save
refusal, capacity rejection before staging, teardown, living resave/reload and
loaded remove(self) dead-spawn cancellation have explicit predicates. Default
nine cases plus invalid-save and original default negotiation also pass.

[limits] Captured/native prepared second endpoint, actual paired loopback first;
no full two-socket gameplay claim. Current ordinary v5/public/disabled/fastload
paths keep existing policy; unavailable authored KEX/hub and device outcomes
remain excluded under user instruction. Prior independent native Shub/music/
catalogue/physics/input/calibration and shipped079 four-platform proofs are
credited at documented boundaries; these are not proof of a final new engine.
Four-platform affected-engine refresh follows approval/commit, then final overall
integration signoff after remaining attainable software cases settle.

Decision for same local Astra/xhigh Kant: verify patch against your prior approved
plan and actual native owners/receipts; identify blocking correctness/lifetime/
ordinary-desktop regressions or unnecessary architecture, and required affected
rerun scope. Rejected broader alternatives remain prior brief's native slot
migration/raw-word remap/duplicate allocator. No new185-feature audit, broad
mod matrix, physical tests or new pending serialization policy. Read-only,
no edits/delegation. <=1600word terminal review with prioritized disposition and
exact file/line evidence. Main owns synthesis/integration; not finalF10signoff.


## Final main implementation disposition and affected rerun

Same local Astra/xhigh Kant verified the implemented adapter and native
allocator/lifetime owners. The review approves bounded integration, not F10.

| Recommendation | Main disposition |
| --- | --- |
| Integrate the typed staging adapter; no blocking ordinary-load regression or unnecessary parallel architecture was found. | Adopt. Main reviews all four production files; normal FIFO allocation and ordinary load policy remain native. |
| Retained-free payloads can still be consumed by QC and must follow pending identities. | Adopt the tiny defensive correction: skip only free edicts with no retain count. One native retained-free owner/enemy container proves relocation before anchor release and native FIFO re-entry after release. No raw/free-garbage scan. |
| Repeat affected lifecycle/cancellation checks; retain unrelated evidence. | Adopt. Corrected private host_cmd and fixtures compile/link against the copied225-object compatible Debug graph. Both actual native cases pass, including strict saved-reference/QC-consumer and retained-free/release predicates. Earlier twelve load cases/default negotiation retain their documented scope. |
| Refresh shipping engines after this production change. | Adopt; affected four-platform build/package/source reconciliation follows this commit. No completion claim from the private fixtures. |

Final corrected receipts are FastGames/qsvr-v06-retained-free-pq5rxsak/receipts/
main-verified.json, with current six source/test hashes, both private binary hashes
and two log hashes. Earlier worker manifest is historical, not the corrected
source manifest. Immutable original225 engine/outputs remain untouched.
