# One weapon calibration in solo and multiplayer

Branch `2.0` uses one held offset and one muzzle offset **per weapon** in solo
and multiplayer (`71eec741`). A weapon's mesh and QuakeC firing expression can
still need individual calibration; network mode does not select a second
physical muzzle. QBJ3 and Enyo shot/barrel alignment remain to be checked in
the user's later device testing.

## Evidence and current boundary

The pinned donor gates its additional `mp_*` held and muzzle vectors on
`cl.maxclients > 1` (`vr.c:3090,3134–3136,3190–3194,6197–6229`). Its client
command serializes the adjusted world muzzle relative to the player pose
(`cl_input.c:1009–1070`). The server reconstructs the remote muzzle from that
relative pose or queries the same client muzzle directly for a local player;
both routes converge on the world clamp and per-weapon QuakeC source correction
(`sv_phys.c:5386–5525`). The local shortcut avoids one frame of loopback delay
(`sv_phys.c:6465–6468`). No distinct network-mode model transform is visible in
those paths. That observation alone does not prove that every installed mod is
aligned with one vector.

Of 37 installed `vr_weapons.txt` files inspected, only QBJ3 and Enyo author MP
offsets. QBJ3 has six nonzero per-weapon muzzle deltas and zero MP held deltas;
some muzzle components exceed 45 Quake units. Enyo has a global MP muzzle delta
`(-4.995956, -7.580808, -64.77399)`. These are large enough that silently
discarding them changes shot origin. The donor's own note at `vr.c:5981` says
per-weapon MP corrections replaced a shared offset because QBJ3's weapon meshes
have different origins. That establishes the value of **per-weapon** calibration,
not a permanent need for **per-mode** calibration.

The earlier `2.0` adapter applied legacy MP overlays when `cl.maxclients > 1`.
Commit `71eec741` removed that runtime branch. The calibration lookup functions
read only shared base offsets; both the rendered viewmodel and command muzzle
use those lookups. The stock `id1` shotgun has no MP overlay. A live private
OpenXR command reached an unchanged pinned donor dedicated server, and 60
observed firing samples reconstructed its pre-clamp
muzzle from the authoritative body and command-relative pose within about
`0.00003` Quake units. This covers one stock multiplayer path, not QBJ3/Enyo
shots, a visible barrel, final projectile origin, or damage.

The `2.0` client has `vrweaponsave` and controller-driven `vradjustweapon` and
`vradjustmuzzle` commands for the equipped classic or enhanced alias viewmodel.
The adjustment session freezes the viewmodel, uses the controller trigger to
commit a grip or muzzle position, and writes the matching fields in the active
game's `vr_weapons.txt`. The writer preserves unrelated blocks and the other
format's fields; an explicit save removes obsolete per-weapon MP keys from the
selected block. Existing and resulting schema text are checked by the same
parser used on reload; the exact active-game file is checked after writing.
The commands build and the schema writer/reload fixtures pass, but live headset
calibration and installed-mod gameplay remain unqualified.

The senior review found a separate baseline gap: Enyo's schema carries weapon
identities and a global MP delta but omits most base held/muzzle values. The
donor initializes ten Enyo models with built-in held XYZ/scale and seeds each
muzzle Z from the held Z (`vr.c:8185–8215,3280–3294`). The `2.0` reload path
now installs those ten classic defaults before applying the Enyo schema. The
focused reload fixture checks all ten held/muzzle defaults, an identity-only
schema block, an inert legacy global MP value, and an authored override. This
restores the calibration baseline needed for later shot-origin comparisons;
it does not prove Enyo gameplay parity by itself.

## Current rule and remaining acceptance

Keep the legacy parser so installed QBJ3 and Enyo profiles still load, but
`mp_*` values are inert in runtime lookups. `VR_WeaponCalibrationLookupHeld`
and `VR_WeaponCalibrationLookupMuzzle` provide the same per-weapon values in
solo, listen-server co-op, and dedicated-server co-op. `r_alias.c` uses the held
lookup for viewmodel placement, while `VR_WeaponCalibrationCurrentMuzzle` and
`vr_input.c` use the muzzle lookup for the transmitted command. The server
reconstructs and clamps that physical muzzle at its existing weapon-use
boundary. The separate `muzzle_source_offset` corrects a mod's QuakeC firing
expression and is independent of player count.

The editor exposes only shared held and muzzle adjustment commands. On an
explicit save, it removes legacy per-weapon MP keys from the selected block;
it does not rewrite unrelated weapon blocks or a global legacy MP key. Loading
an existing file does not change that file. Focused calibration and reload
fixtures verify that MP fields do not alter the shared lookups.

For release acceptance, compare the same QBJ3 pistol and an Enyo projectile
weapon in solo, listen-server co-op, and dedicated-server co-op. Hold weapon,
hand pose, pitch, handedness, model format, and configuration constant. Include
clear space, a nearby wall, wrist rotation, and body translation. Observe the
rendered barrel, pre/post-clamp muzzle, QuakeC trace or projectile origin and
direction, impact/damage, and world collision. The QBJ3 pistol tracer alone is
not proof of its damage trace; the donor uses separate firing expressions.
Extend the check to flak and each distinct firing path. A stock shotgun packet
does not establish mod alignment.

The donor's MP deltas can be large, so removing them changes its multiplayer
shot origin. That is an intentional move to the requested single calibration,
not proof that every mod's new alignment is correct. If a mod needs a different
QuakeC firing correction, fix that at the narrow source boundary instead of
reintroducing a network-mode held or muzzle offset. The user will perform
physical headset and multiplayer shot checks later.

## Astra senior-review disposition

The local review ran with effective `gpt-6-astra` / `max` settings and checked
the donor, port and installed QBJ3/Enyo schema evidence. Its key additional
findings were the missing Enyo baseline and the listen-host transport split.

| Recommendation | Disposition |
| --- | --- |
| Keep effective MP overlays until affected mods have shot and visual parity proof | Superseded by the user's single-calibration target and `71eec741`. Legacy parsing stays, but the MP overlays are inert. |
| Compare one shared calibration through the existing renderer and weapon-use path | Adapted. Donor Enyo defaults are now installed and checked by a focused fixture; include solo, listen-host and dedicated modes so network transport and `cl.maxclients` are not conflated. |
| Convert proven entries individually while preserving provenance | Superseded for runtime selection. Classic and enhanced base profiles remain distinct by geometry format; explicit editor saves omit obsolete per-weapon MP keys. |

If a mod cannot align its barrel and shot under one calibration, correct its
QuakeC source expression at the narrow firing boundary rather than adding a
mode-dependent muzzle again.
