# q30 scheduled empty-ammo handoff

Status: committed before production; bounded handoff implemented and locally
reviewed by Astra, with Linux component checks passing. Actual
preceding native-QC sequence proves the gap, with ordinary inventory prepared.
Full q30 admission remains closed. This
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
reaches due player_nail2 at frame12. Actual QC selects weapon2 and classification
becomes native. Its initial strict-error assertion also encountered an unselected
profile; the final corrected fixture below proves the intended movement-class
rejection. Exit0 and Q30_EMPTY_AMMO_AUTHORED_PREFIX_PASSED verify that prefix;
this is not normal offer/spawn/begin evidence.

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

## Implementation checkpoint evidence

The existing due-window predicate now groups20 exact projectile identities by
their corresponding current ammo value. Only ammo<1 qualifies those callbacks
for the existing native frame; actual QC still picks the replacement weapon.
No new state/cache/queue/clock, later input or extra movement interval was added.

The existing q30 fixture's separate -emptyammo mode retains the actual native
impulse4/last-shot prefix and nail2 deadline for its first case. All20 root cases
and an additional ordinary-shotgun fallback compare actual raw callback,
native/selected body/velocity/flags/weapon/all ammo/completion, analog movement
and configured Gorilla on/off. Exact one-ammo and ample-ammo cases execute actual
launch QC while remaining selected. A not-due callback first completes held input,
then its due quiet native frame keeps the completed cursor and input levels
without inventing command history. Quiet-frame movement parity is not compared.
Future/consumed opportunities and safe run animation do not gate. Later roots,
inventory/selection and quiet deadlines are prepared component seams; this is
not authored pickup/traversal, real pose/contact or normal admission evidence.

Linux SDL3 production and fixture builds exit0 without warnings. Empty-ammo mode
exits0 with prefix,21 callback and aggregate markers. Scheduled-camera mode
retains all four actual callback markers. The normal seven q30 replay cases and
native-state checks pass. Stock mixed pause/arrival-gap/native-return, bounded
velocity writer/parser and q30 negotiation/stats/full-parser checks pass.

## Final local Astra disposition

The read-only final local Astra Max review found no blocking production defect
and one false-positive fixture assertion. Main independently verified final
effective gpt-6-astra/max metadata and checked the reset/validation source order
behind that finding. No raw telemetry is included; Astra ran no software checks.

| Recommendation | Disposition |
| --- | --- |
| Raw-QC strict rejection was passing because the restored profile was unselected. | **Fixed.** Select the profile and require initial strict validation success. After actual QC, require frame validation success and NATIVE classification plus strict failure for weapon2. Ordinary-shotgun fallback remains WALK and passes strict validation. |
| The existing due-window/ammo/identity predicate is the minimal adapter. | **Verified/adopted.** Actual bytecode thresholds match all20 names and the existing fresh-native/deferral/completion ownership is retained. |
| Quiet case did not compare movement or retained analog levels. | **Adapted.** Add assertions for retained actual completed forward/attack levels and cursor. Keep movement-parity claims limited to the commanded native/selected comparisons; no quiet-body comparison is claimed. |
| Zero/ten ammo did not exercise the exact usable boundary. | **Addressed.** Actual selected launch now runs with ammo1 and ammo10 for all20 roots and the ordinary-fallback case. |
| Preserve component, pose and admission limits. | **Adopted.** Only first nail2 has the actual preceding-shot deadline; other roots/quiet scheduling are prepared, Gorilla uses synthetic configuration, and normal admission/traversal remains open. |

The corrected fixture rebuilds without warnings and -emptyammo exits0 with all
21 callback markers and the aggregate marker. Production is unchanged after the
review. Normal replay, scheduled-camera, mixed and negotiation checks above were
already green for this same production code; review fixes affect only this mode.
