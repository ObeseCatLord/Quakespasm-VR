# q30 ordinary callback closure

Status: ordinary PreThink closure verified; scheduled weapon Think closure is
in progress. Normal admission remains closed. This follows the local Astra
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
