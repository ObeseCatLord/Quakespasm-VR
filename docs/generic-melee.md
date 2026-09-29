# Melee in uncovered mods

With immersive melee enabled in VR options, uncovered standard `progs/v_axe.mdl`
and `progs/v_axe2.mdl` weapons have a swing-to-attack fallback. It uses the mod's
normal attack input, damage, range, timing, effects and animation. The real
trigger remains usable. Known physical-contact adapters take priority when
their selected geometry and server capability are available.

Other weapon models can opt in through the existing `vr_weapons.txt` file:

```text
{
    viewmodel progs/v_sword.mdl
    melee 1
    melee_speed 1.0
}
```

`melee 0` disables generic recognition for that model, including the standard
axe convention. `melee_speed` sets the gesture threshold in metres/second,
between 0.25 and 10; its default is 1.0. Only the fallback is affected by these
profile fields. VR options' immersive-melee toggle controls both the fallback
and the exact adapters.

Gesture sensing normally uses the grip and the calibrated muzzle point.
Optional `melee_base x y z` and `melee_tip x y z` specify points in scaled
model-local coordinates, before the renderer's held transform. They improve
rotation sensing for long weapons. These points do not change native damage
range. Use the same selected geometry/calibration as the visible weapon;
replacement meshes may need different endpoints. No multiplayer-only offsets
are introduced.

A gesture requests an ordinary attack-button pulse, not direct blade damage.
Native cooldown may reject a pulse; some mods require holding attack or schedule
their own multiple effects. The fallback neither retries until damage occurs
nor replaces these behaviors. Very rapid gestures before a command is sent
coalesce like ordinary input edges. Exact swept-surface strikes still require
an adapter for the mod's acquisition contract. Desktop input is unchanged.

Implementation and final review precede consolidated Linux/ARM verification.
This document does not claim headset or unknown-mod runtime qualification.
