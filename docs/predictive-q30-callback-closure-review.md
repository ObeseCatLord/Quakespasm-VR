# q30 ordinary callback closure

Status: ordinary PreThink closure verified; bounded third-axe native dispatch
implemented, with remaining scheduled weapon Think closure in progress. Normal
admission remains closed. This follows the local Astra
decision in the [transition plan](predictive-q30-transitions-2.0-plan.md).

## Ordinary PreThink audit

One read-only local Astra Max audit verified the installed SHA-pinned binary.
Main independently checked final effective `gpt-6-astra` / `max` settings, the
actual allocation/link/relocation hooks and the direct PreThink bytecode. A
resumed older reviewer had reverted to Sol and refused before investigation;
that attempt is not audit evidence. No raw session telemetry is exported.

| Authored path | Verified closure under current ordinary WALK exclusions |
| --- | --- |
| Attachment, PCs54282–54288 | Attachment allocation1672:80222–80225 uses spawn, excluding client slots. Solid0/movetype8 stores80232–80237 apply to the attachment. PF_setorigin relinks with triggers disabled; stock-nail override requires another function/PC. Weapon hooks only mark matching scopes; relocation invalidates actual client entities only. |
| CheckRules1138:53727–53748 / NextLevel1137:53652–53726 | Non-deathmatch/gameover returns; otherwise find/spawn identifies a separate changelevel entity. Stores53721–53725 schedule that entity, not the player. Actual supplied config masks bypass the printing/cvar branch. No immediate target callback or player Think replacement. |
| Dry WaterMove1146:55090–55158 | Air/damage bookkeeping, optional sounds and FL_INWATER clear, then return. Liquid damage, drowning, drag and CheckWaterJump are unreachable for the qualified dry body. |
| Normal PlayerJump1145:55044–55074 | Ground/release guards; flag/button changes and sound; velocity_z += map_jumpheight at55070–55073. No hull/mode or scheduling write. Existing shared solver preserves this impulse and avoids a second jump. |
| Remaining ordinary instructions | Grapple eligibility returns51812; coherent effective gravity is restored54409–54417; release rearms54422–54425. Future hold-zero is excluded. No reachable OP_STATE, player removal, self reassignment or player Think/deadline store. |

This closes authored ordinary PreThink, not malformed self-referential custom
entity edits or every scheduled callback. It permits a pre-staging scheduled
Think decision without an ordinary PreThink subsequently replacing that Think.

## Demonstrated scheduled exception

The existing exact-q30 fixture now has a `-scheduledcamera` decision check.
With prepared monster HP-target and camera entities on a real stock hull, the
actual player_axe3 scheduled Think executes the pinned axe trace, T_Damage,
HP-threshold target dispatch and misc_camera_use. Target health decreases and
cam_active becomes1. The ordinary current-state classifier changes from WALK
to NATIVE; current strict post-Think StateError rejects it. No synthetic force
or replacement QC callback produces that result. The camera check sets QC coop0
because q30 deliberately ignores camera activation in co-op. Entities/target
properties and selection are prepared component seams, not normal admission or
an authored map traversal.

Actual binary five third-axe callbacks1351/1355/1359/1363/1367 call W_FireAxe at
71107/71132/71157/71182/71207. W_FireAxe1323 calls T_Damage at69057. This is a
specific pre-movement native eligibility requirement. Reflected axe was checked
separately and supplies effects only; it is not a movement-force justification.

## Next implementation contract

Add one observational due-Think predicate at the existing physics dispatch
boundary for these five third-axe callbacks only. Resolve their names against
the current VM function map. Use the existing unconsumed world Think opportunity
and its exact deadline horizon, including overdue/clamped Think behavior; do
not consume that opportunity in the predicate. Validate current q30 Think
deadline/function bounds before dereferencing. No persistent binding cache or
parallel scheduling state is needed.

At offset0, reuse the fresh native input/QC/physics/contact/completion owner.
Any justified later unstarted head uses the existing defer boundary; never
append a world interval to prior command movement. Ordinary animations and
not-due callbacks keep selected PMove. An unsafe due callback is selected before
zero-time maintenance too, so its real native world frame retains original
input order. No living residual, late ClientThink or callback replay is added.

Production write set: `Quake/sv_phys.c` only. Test write set: the existing
`tests/q30_movement_native_fixture.c` and README. Main integrates locally because
the requested Luna route is unavailable. Final local Astra implementation review
and consolidated Linux checks follow coherent implementation.

Acceptance: actual native-versus-selected callback activation with nonzero
analog movement and observable body/weapon outcomes; all five function identities;
due/not-due and consumed-opportunity decisions; safe animation remains selected;
no-command maintenance/cursor ownership; malformed deadline/function rejection;
Gorilla on/off and existing ordinary replay/regression cases. Keep prepared
fixture/transport boundaries explicit. Other scheduled damaging callbacks, full
map traversal and normal admission remain required; this gate alone cannot
finish ordinary q30 activation.

The attempted water-crossing searches did not qualify a reachable geometry in
the tested maps. Their new search code was removed. The existing wet-position
and probe-restoration evidence remains bounded; actual horizontal entry is
still open and must not be reported as passed.

## Implementation review and checkpoint evidence

The narrow predicate and pre-staging integration are implemented in the existing
physics owner. A final fresh local Astra Max reviewer found no blocking issue
in the bounded change. Main checked final effective `gpt-6-astra` / `max` metadata
and spot-checked native Think equality/overdue clamping, the unconsumed window,
function bounds and dispatch position. Astra did not run the software checks.

| Final review finding | Disposition |
| --- | --- |
| Deadline uses world duration, includes overdue/equality, and leaves opportunity untouched. | **Verified/adopted.** Same existing native scheduling horizon; no new callback clock. |
| Validate deadline/function before forming a function pointer. | **Verified/adopted.** Current q30 frame validation rejects malformed scheduling, with an independent lookup bound in the predicate. |
| Dispatch precedes staging, credit and maintenance. | **Verified/adopted.** Fresh native retains ClientThink before QC; existing deferral remains the only later-head owner. A changed movement class is not required to choose native. |
| Keep full admission outside the component claim. | **Adopted.** Other scheduled callbacks, actual map traversal and normal-session activation remain unqualified. |

Linux SDL3 production build exits0. The normal exact-q30 fixture passes its six
native-state markers and all seven ordinary replay comparisons. The separate
scheduled-camera mode exits0 and compares actual body/velocity/flags/weapon/
completion between native and selected dispatch with nonzero analog input,
including observable movement in the desktop case, and Gorilla enabled/disabled.
Actual safe axe2 animation remains selected; future/consumed scheduling decisions
do not gate. A no-command due attack executes native QC with unchanged completed
cursor. Nonfinite deadline and out-of-range scheduled function checks reject.
Stock mixed-session pause/arrival-gap/native-return and q30 negotiation/writer/
full-parser/refusal checks pass. No full q30 admission or device result is claimed.

The safe-animation test uses an 8ms command. The original test's `.010f` converted
to double supplies slightly less than10ms of world credit; maintenance correctly
does not complete its10ms head. This was a fixture timing mismatch, not a reason
to loosen production command credit. Native due-callback dispatch independently
precedes command-time credit as required by its existing world-frame contract.

Next: close the remaining actual scheduled weapon callbacks and add only
source-demonstrated exceptions to this same boundary. Complete traversal and
normal offer/spawn/begin/serialization/parser/replay before admission. The
preexisting stock10ms liquid assertion remains separately unresolved.

## Scheduled hitscan extension plan

Plan before implementation, following checkpoint `2ddd6e50`. Preserve ordinary
predicted movement, native input ordering and actual q30 target behavior in both
desktop and VR. Normal admission is still closed until the full phase/traversal/
session contract is qualified. This extension completes the already demonstrated
synchronous damage-to-target family; it does not replace movement or QC owners.

Verified installed bytecode extends the third-axe finding:

| Scheduled root | Actual path to synchronous target invocation |
| --- | --- |
| player_sg1 1380, PCs71442–71453 | W_FireShotgun1330 calls FireBullets1329 at69452 when configflag bit131072 enables hitscan. FireBullets calls ApplyMultiDamage1325, which calls T_Damage926 at69211. |
| player_light1 1412 / player_light2 1413, PCs71684–71721 | W_FireLightning1339 calls PlayerLightningDamage1338 at70258. Its damage paths call T_Damage at70122/70137; T_Damage37735 can synchronously invoke trigger_strs with the attacker. |

The bitmask value comes from binary operands, not the decompiler's misleading
constant alias. Projectile shotgun, cooldown and released-button branches may
avoid damage, but the same due callback can reach damage from valid ordinary
state. Qualify these three exact due identities at the existing boundary. Avoid
duplicating config/weapon/cooldown policy in a second scheduler. Safe reset and
animation frames, not-due callbacks and consumed Think opportunities continue
to use the selected owner. A broader all-firing gate was rejected because
ordinary projectile spawn-only callbacks may remain compatible.

Implementation stages and ownership:

1. Reuse the scheduled-camera fixture's target and actual QC path for shotgun
   hitscan and both lightning animation roots. Prepare the real config bit,
   ammo and attack level; prove synchronous camera activation before movement.
   Compare native and selected body/velocity/flags/weapon/health/ammo/completion
   with nonzero analog input and Gorilla disabled/enabled. Existing target
   staging is a component seam, not authored traversal or normal admission.
2. Extend only the existing identity list in `Quake/sv_phys.c`. Reuse the world
   Think window, fresh-native frame and later-head deferral. Add no state/cache,
   clock, movement interval, force adapter or QC replacement.
3. Extend the same fixture's safe animation, future/consumed opportunity and
   no-command checks. Keep the normal exact-q30 replay matrix and stock/private
   negotiation checks as consolidated Linux acceptance. Get a local Astra final
   review of the bounded integration and record the disposition.
4. Independently audit dry projectile callbacks and builtin/native weapon
   interception for synchronous effects. Record the evidence before deciding
   whether another exact exception is necessary. Do not claim full scheduled
   closure from these three cases or broaden production scope without a revised
   committed contract.

Exact write set: `Quake/sv_phys.c` and
`tests/q30_movement_native_fixture.c`, with this record, the implementation-plan
index and test README. Expected production expansion is three identities in
the current predicate. Existing ordinary PreThink closure is a dependency;
headset, performance and Windows/ARM checks stay deferred as instructed.
