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
