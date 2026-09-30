# Equipped-weapon ammo in the shared wheel

2026-09-30. Before-code WPN-004 repair. Preserve the existing catalog, stat
transport, prepared frame and both draw owners; no new per-weapon ammo cache.

## Verified incompatibility and smallest adapter

Primary `51b452c0:Quake/vr.c:VR_GetWeaponAmmo` explicitly refuses `STAT_AMMO`
for an inactive weapon: QuakeC `currentammo` belongs to the equipped weapon,
not every wheel entry. Current `VR_WeaponMenu_BuildVisible` copies that stat
for all entries regardless of its already-computed `active` flag. The supplied
QBJ3 pistol profile uses `STAT_AMMO`, so equipping another gun can put that
gun's ammo under the inactive pistol. Authored magazine schemas have the same
boundary. Reserve stats and dynamic reserve maxima are separate and remain.

Add the inherited eligibility condition to the existing visible-row ammo
assignment: expose ammo only when the entry has a stat and either its stat is
not `STAT_AMMO` or it is active. Otherwise use the existing -1 hidden sentinel.
The playspace scene glyphs and selected desktop/view-panel label already skip
that sentinel. Keep weapon ownership, switching/readiness, model identity and
ammo maxima unchanged; unknown inactive magazines must still be selectable.

Ownership: `Quake/vr_weapon_menu.c` only, at most8 changed production lines.
Expected cost is one condition per prepared visible row, no per-eye work or
state. A second ammo registry or protocol field is unnecessary. Stop/report
if the draw consumers or stat semantics contradict this exact boundary.
Local coding agent implements; main reviews the complete diff and reference.

## End-of-implementation qualification

No builds/tests/probes now. At final Linux/ARM software qualification, use
registered QBJ3/authored `STAT_AMMO` entries with another weapon equipped,
then equip the magazine weapon; verify hidden inactive/current active ammo,
ordinary reserve quantities/dynamic maxima, empty magazine selectability,
model/selector transitions and both shared presentation paths. These source
conditions alone do not establish actual mod or rendered behavior.

## Source integration

The repair adds three lines and removes one in the existing visible-row
assignment. Main inspected the complete diff, primary's equipped-only rule,
native `SV_CalcStats` currentammo/reserve producers and both draw sentinel
consumers. Ownership, selectability and capacity resolution remain unchanged.
Scoped whitespace checking passed; no builds/tests or executable checks ran.
Final actual program/transport/presentation qualification remains open.
