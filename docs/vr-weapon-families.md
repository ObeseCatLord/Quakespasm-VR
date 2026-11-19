# VR weapon calibration families

Many mods ship weapon viewmodels that are another mod's mesh under a new path,
a reskin, or a mission-pack model reused elsewhere. Drake and Quoth weapons
appear in several mods, AD's weapons appear in a dozen forks and in Dwell, and
Rogue's `v_multi` is the stock rocket launcher mesh. Calibrating by file path or
game directory misses all of these. A family calibrates a mesh instead.

## Identity

`VR_WeaponFamilySignature` hashes a version 6 MDL's vertex and triangle counts
and the first frame's decoded vertex positions, quantized to 1/8 unit (64-bit
FNV-1a). Skins, normals and animation do not participate, so reskins and
re-encoded headers share a family. `Misc/vr-weapon-families/quakefs.py`
computes the same value.

On every game calibration reload the engine scans the active `progs.dat` for
viewmodel path literals (`progs/...v_*.mdl` or `*_v_*`), hashes each file once
and caches the matches. Lookups during play do no file IO.

## What a family supplies

Each row of `Quake/vr_weapon_families.h` holds, in the reference mesh's model
space (x forward, y left, z up):

- a grip and held scale. The held offset is derived per file as
  `held = -scale * grip - (1 - scale) * origin`, which is exact for that file's
  own quantization origin (`R_AliasModelMatrixInternal`).
- a barrel tip (the blade or head for melee). The classic muzzle is the tip
  carried through the slot's current grip and scale, at the default world scale:
  `muzzle = scale / 0.75 * (tip - grip)` converted from forward/left/up to
  right/up/forward. Because the muzzle follows the current grip, it stays on
  the barrel under the Vanilla/Enhanced/Authentic/Plague presets and after a
  `vradjustweapon` grip change.

Stock meshes (id1, Hipnotic and Rogue weapons) have tip-only families: their
grips stay on the user-selectable presets. The same stock mesh under another
path, such as Mjolnir's `progs/hipnotic/v_prox.mdl`, borrows the stock path's
current grip.

## Precedence

1. Path presets (stock presets, folder presets, AD byte identity) apply first.
2. Families set grip and scale for matched paths, except where a preset was
   tuned on that very mesh: byte-identified AD assets and the Peril, QBJ3,
   Alkaline and Enyo folders, and Snack's two nailguns.
3. The mod's `vr_weapons.txt` overrides grips and muzzles as before, so values
   saved by `vradjustweapon`/`vradjustmuzzle` stay authoritative. Two kinds of
   entries carry no calibration and are ignored for a matched path:
   - a complete copy of the old generic defaults: a half-unit grip, its scale
     and the implicit muzzle `(0, 0, held Z)`;
   - a `(0, 0, z)` muzzle whose `z` is a built-in preset's held Z, which is
     what a grip-only adjustment left behind.
   Any other value, including an all-zero "no barrel" muzzle, is kept.
4. Finally every matched path whose muzzle is still implicit receives the
   barrel muzzle.

`vr_weapon_families` lists the active game's matched viewmodels with their
effective grip, scale and muzzle; `(barrel)` marks a family-derived muzzle.

## Dwell

Dwell's folder preset has been retired. Its weapons are AD and Alkaline meshes
plus its own twin axe and rail gun. The old rows gave the AD rocket launcher
mesh the stock shotgun grip and put the axe and rail grips outside the models.
Families now calibrate all of them, and Dwell's AD meshes inherit AD identity
like any other AD fork.

## Snack

Snack keeps its folder preset only for its own Stakegun (`v_nail`) and
Impaler (`v_nail2`). The earlier values held the Impaler by the rear tip of its
stock at roughly twice the size of the stock nailguns and put both muzzles far
past the barrels. They now hold the Stakegun's right handle and the Impaler's
pistol grip at scale 0.11, with muzzles on the barrel face and bolt head. Its
other weapons follow their families.

## Coverage of the installed mods (2026-11-18)

127 families: 16 stock meshes (tip only), 52 rows re-expressing existing tuned
presets (AD, Alkaline, Copper, Enyo, Snack, QBJ3, Peril) and 59 new landmark
picks. The picks cover every viewmodel referenced by an installed mod's QC that
had no calibration of its own:

- the Drake/Quoth arsenal shared by Tershibboleth, Nyarlathotep, Something
  Wicked, Zerstorer and Mjolnir (plasma/laser, lightning, rockets, chainsaw,
  wand, crossbow);
- Quake 1.5's LQ1 weapon set; MG3's blood shotguns and hammer; the Zerstorer
  chaingun, double shotgun, lightning and stun guns; Tomb of Thunder's chaingun,
  rail and shotgun; the QuakeVR grapple, lasers and multi-prox; Warpspasm's
  sword; Reliq's shotgun; Quoth's bomb;
- Mjolnir's packs that its QC actually uses (Age of Apocalypse melee and
  guns, its crossbows, Quake 1.5 saw, Madfox rocket, airfist, AD 1.71 variants);
- Dwell's twin axe and rail gun.

Where a mod already carried a saved `vradjustweapon` grip for a new mesh
(Dwell's shotguns, Ravenkeep's Redfield weapons, MG3's laser cannon), that grip
became the family grip so other mods using the mesh get it too.

Excluded: projectile models that QC names with a `v_` prefix (`v_spike`,
Drake's `v_mirv`), the `v_ssmoke` effect quad, and Bonk's hammer cosmetics,
which use held-melee recipes.

## Limits

Grips were chosen on ready-frame wireframes, not in a headset. Muzzles assume
`vr_world_scale 1`, as did the existing tuned muzzles. Enhanced (MD5)
replacement models keep their separate enhanced offsets. Server projectile
source compensation (`VR_WeaponCalibrationProjectileSourceOffset`) is
unchanged; the client sends the same calibrated muzzle in single player and
multiplayer.

## Maintenance

`Misc/vr-weapon-families/generate.py` rebuilds the header from an installed
game tree (read-only). `calibrations.py` holds the tuned rows and the picks.
To add a family, add a `Pick` with the reference model, grip, scale and tip,
regenerate, rebuild and run:

```sh
python3 Misc/vr-weapon-families/generate.py --base "$QSVR_GAME_BASE"
python3 tests/run_vr_weapon_families.py --base "$QSVR_GAME_BASE" --binary build-debug/vkquake
```
