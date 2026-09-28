# q30 scheduled empty-ammo handoff

Status: plan before production. Actual preceding native-QC sequence proves the
gap, with ordinary inventory prepared. Full q30 admission remains closed. This
extends the [callback closure contract](predictive-q30-callback-closure-review.md)
and retains its traversal/session requirements and the broader AD-family goal.

## Behavior and evidence

Desktop and VR private players must keep moving when a scheduled projectile
weapon runs out of ammo and QC auto-selects another weapon. Preserve actual QC
selection, recoil, native input-before-callback ordering and one completion
cursor. Successful dry projectile launches should keep ordinary prediction;
public desktop and stock peers retain their current owners.

Verified installed SHA-pinned q30 bytecode: nail roots1393–1400 and snail roots1402–1409 reach
W_FireSpikes69565; grenade1422, rocket1415 and plasma1410/1411 reach fallback
calls69646/69717/69772. W_BestWeapon70439–70445 returns2 for shells>1 and
super-shotgun ownership. forceweaponswitch68942 stores weapon2, excluded by
the current Q30State predicate. Strict post-Think validation then rejects WALK.
The local Astra projectile audit verified these branches and the successful
spawn-only alternatives; main independently checked their load-bearing stores.

Main's temporary Linux proof prepares one nail and ordinary weapon ownership,
then uses actual QC impulse4, fires the last nail, runs native held input and
reaches due player_nail2 at frame12. Actual QC selects weapon2 and current strict
validation rejects it. Exit0 and Q30_EMPTY_AMMO_AUTHORED_PREFIX_PASSED verify that
prefix; this is not normal offer/spawn/begin or a selected/native comparison yet.

## Minimal adapter and rejected alternatives

Extend the current due-Think predicate with exact scheduled projectile identities
only when their actual fallback ammo test is true: nails<1 for nail/snail, rockets<1
for grenade/rocket, cells<1 for plasma. Reuse existing world opportunity and
fresh-native/later-head deferral. Do not copy W_BestWeapon or require a guessed
destination weapon. Native QC remains the only selector. Released/cooldown paths
may conservatively use one native frame at low ammo; the gate repeats no input or
callback. No ability cache, queue, scheduler or new movement owner is needed.

All due projectile callbacks native would unnecessarily discard proven successful
launch prediction. Allowing selected movement through arbitrary native state
would broaden the strict body/force contract. Late ClientThink would reorder
input after QC. These alternatives were rejected for this specific evidence.
This follows the existing Astra prequalification design disposition; final
local Astra review must verify the conditional integration and proof limits.

## Implementation stages and acceptance

1. Move the authored-prefix proof into the existing q30 fixture, retaining the
   prepared inventory seam and actual impulse/last-shot/QC scheduling. Reproduce
   the gap before claiming the fix. No replacement weapon callback.
2. Add the conditional identities to the existing predicate in sv_phys.c:
   player_nail1..8, player_snail1..8, player_grenade1, player_rocket1 and
   player_plasma1/2. Successful-ammo cases, safe animation, future deadlines and
   consumed opportunity must remain selected. Do not duplicate best-weapon policy.
3. Compare actual native and selected fallback from the same last-shot prefix
   with nonzero analog input, including Gorilla disabled/enabled, body/velocity/
   flags/weapon/ammo/completion, queue retirement and zero credit. Verify quiet
   due fallback uses real completed levels without advancing ACK. Cover each
   ammo family and a fallback destination that remains ordinary where practical.
4. Run consolidated Linux production/fixture checks and local Astra final review;
   record disposition. Normal replay and prior callback cases must still pass.
   Close remaining scheduling/actual traversal and complete the normal-session
   serialization/parser/replay vertical before enabling q30 admission.

Exact write set: Quake/sv_phys.c, tests/q30_movement_native_fixture.c, tests/README.md
and linked planning/review records. Expected scope: three ammo comparisons and
20 exact identities, plus reuse of existing native fixtures. Reopen before new
body policy or another owner is required. Hardware/eye tracking, performance
measurement and Windows/ARM checks remain deferred as instructed.
