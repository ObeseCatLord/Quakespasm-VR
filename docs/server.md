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
