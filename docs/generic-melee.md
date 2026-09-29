# Gesture-only melee

With immersive melee enabled in VR options, recognized melee weapons use swings
to request the mod's normal attack input. Physical attack triggers are disabled
for these weapons, and the held VR model stays in its ready pose. Native QuakeC
retains damage, range, timing, sounds, effects and its internal attack sequence.
There is no swept-blade damage replacement. Turning immersive melee off restores
ordinary trigger attacks and animation; desktop input is unchanged.

Standard axe paths and existing known axe/wrench/sword/fist conventions are
recognized by default. Other models use the shared profile opt-in below; no
per-mod engine patch or server capability is required for a gesture pulse.
Physical-contact adapters are deferred under the current migration scope.

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
between 0.25 and 10; its default is 1.0. `melee_frame N` selects the held ready
frame (integer 0..65535, default 0). A frame beyond the selected asset's frame
count uses frame 0. Existing generated split weapon/fist meshes keep their
recipe's ready pose. The shared classification controls gestures, physical attack
suppression and steady VR weapon presentation.

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
coalesce like ordinary input edges. The mod determines the target and reach;
there is no universal physical parry or edge-contact guarantee.

Single weapons sense the dominant hand. Known paired melee weapons sense both
hands independently; simultaneous swings request one native attack input. The
mod chooses its native attack sequence and hand, rather than receiving a new
per-hand damage protocol. Trigger bindings for jump and menu/calibration controls
remain usable; gameplay attack bindings are suppressed for gesture-mode melee.

Implementation and final review precede consolidated Linux/ARM verification.
This document does not claim headset or unknown-mod runtime qualification.
