# Server settings

Set these variables in the server console or a server configuration file.

## Classic co-op

Select traditional Quake co-op behavior:

```text
sv_coop_classic 1
```

Use `sv_coop_classic 0` to return to the default streamlined profile.
Profile-controlled co-op feature cvars use `-1` to inherit the profile;
explicitly setting one to `0` or `1` overrides the profile for that feature.

## VR akimbo

| Variable | Default | Behavior |
| --- | --- | --- |
| `sv_akimbo` | `1` | Allow engine-supported VR akimbo. Set `0` to disable it globally. |

This global policy covers all engine-supported akimbo weapons, currently
QBJ3's twin nailguns and berserk fists, Enyo's SMGs, and Dwell v2.2's berserk
axes. Unsupported versions retain their original weapon behavior.
The server enforces it when applying weapon poses. It does not change desktop
behavior or turn ordinary weapons into akimbo weapons. Changes are reliably
advertised to connected clients without reconnecting or restarting the map.
Put the setting in your server configuration to apply it on future launches.

Disable VR akimbo without restarting the map:

```text
sv_akimbo 0
```

Clients need two tracked controllers. Supported original viewmodels are split
automatically in memory; no extra model download is needed. `vr_akimbo 0`
disables all client akimbo. The older `vr_qbj3_akimbo 0` setting remains a
QBJ3-only opt-out. Neither can override the server restriction. Older servers
that do not advertise a weapon's support retain the original paired model.

Dwell's berserk grips are centered independently on the controllers and stay
attached throughout the attack animation. The weapon's held scale is retained;
old paired-model position offsets are not applied to the split grips.
Grip/muzzle translation adjustment commands explain this controller lock rather
than changing offsets that would have no effect. Disable client akimbo first
only when calibrating the original paired viewmodel.

## Physical weapon contact (development)

For adding or auditing mod support, see the
[VR collision and melee engineering guide](vr-melee.md).

The current development implementation is opt-in on the server:

```text
sv_weapon_collision 1
sv_immersive_melee 1
```

Both server variables default to `0` while compatibility work is in progress.
Clients can independently disable Weapon Collision or Immersive Melee in VR
Options (`vr_weapon_collision` and `vr_immersive_melee`). Unsupported weapons
and servers retain ordinary trigger attacks; enabling contact does not replace
a mod's game code or change desktop weapon attacks.

Compatibility is checked against the installed game code and weapon models;
an unfamiliar revision keeps its normal trigger attack. The current development
coverage includes the original axe (also in the audited DOPA/MG1, RM 1.2,
Hipnotic and MG3 builds), QBJ3 wrench and split berserk fists, Enyo katana,
all 29 Bonk hammer appearances, Dwell axe/berserk, the audited Copper axes,
AD/Ravenkeep axe variants, Alkaline/LimJam axes, Immortal axe/hammer, and the
audited shared Drake axe/hammer, Hipnotic/MG3 hammer variants, and Mjolnir mod's
axe/shadow axe, Gungnir, Fire Scimitar, Blood Mace, Ghost Rapier and hammer.
The official rerelease enhanced axe uses its verified ready pose and the same
user calibration as its rendered model.
Honey's axe retains its original downed-zombie gib behavior. This is not yet complete
coverage of every melee weapon or enhanced replacement model.

Physical hits retain the mod's damage, effects and recovery time. Bonk uses a
lighter tap or a larger swing to choose its original charge tiers, and retains
its floor hop and airborne missed-swing dash. QBJ3's two physical fists also
require `sv_akimbo 1`.

Chainsaws remain trigger-operated: no manual swing is required, and their
native attack cadence and release behavior are retained. They still participate
in general weapon collision and touch-button poking when enabled.

Gungnir uses the trigger for its shard projectile and physical motion for its
stab. Physical stabs do not consume shards. Triggering it does not schedule the
canned spear swing or its follow-up combo; an empty trigger does not stab.
Both actions share the mod's reload time and retain its underwater boost.
Other supported hybrids keep their complete original trigger combos, including
their melee stages. Physical swings perform a single contact attack without
scheduling those animations or later combo hits. Their native cooldown also
prevents a physical attack from overlapping a trigger combo.

For example:

- Fire Scimitar keeps its lunge and Tome projectile on a physical swing; the
  trigger still performs its original multi-strike combo.
- Blood Mace keeps its airborne lunge and Tome healing. A physical strike hits
  the contacted living target, not the original animation's surrounding area.
- Ghost Rapier keeps its ghost projectile and blood-crystal cost (including
  the Tome's zero-ammo behavior), with one physical stab per swing. The trigger
  retains all three native stabs.
- Hammer damage, charged variants and ammo costs follow each mod's game code;
  MG3's glow does not by itself select the charged attack. Physical hammer
  misses do not throw the weapon: throwing remains a trigger action. Drake's
  held-trigger floor slam retains its original button requirement.

Supported physical melee weapons can parry other supported, tracked melee
weapons in competitive multiplayer. In co-op, parrying follows player-collision
policy: it is disabled with the normal no-player-collision profile and enabled
with classic co-op. An explicit `sv_coop_noplayerclip` setting overrides that
profile (`1` disables player collision and parrying; `0` enables them).
Both server contact variables must be enabled. Friendly-fire damage policy is
unchanged, and desktop players do not acquire an invisible defensive weapon.

A parried swing consumes its normal attack cooldown and sends contact haptics
to both players. Disconnected, stale, dead, or unreachable controller poses
cannot serve as guards. Parrying uses the latest validated controller geometry
processed by the server; it does not rewind player poses.
