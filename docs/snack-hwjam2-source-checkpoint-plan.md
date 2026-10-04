# Snack Pack and HWJAM2 source checkpoint

## Scope and reference

This ports the owned portions of main commit `bd923e924410fd210248c0cd9e4d966e62eaf9e3` ("Calibrate Snack weapons and align HWJAM2 wheel profiles") into the 2.0 owners.  The reference changes a monolithic `Quake/vr.c`; 2.0 separates its wheel profile ownership into `Quake/vr_weapon_menu.c` and its held/muzzle/projectile-source calibration into `Quake/vr_weapon_calibration.c`.

## Incremental design

The existing profile catalog and calibration schema are retained.  No parallel roster, calibration storage, or multiplayer offset path is introduced.

- `snack` and `snack3` receive a complete nine-entry profile.  Stock selectors retain `items` ownership.  Rotary Shotgun selector 128 instead uses exported `moditems` bit 128, so an armor bit with the same numeric value cannot make it appear.  The profile uses native impulse 3 for the double/rotary toggle.
- The donor's selectable-ammo gates are retained: double shotgun needs 2 shells, Impaler 10 nails, and Rotary 4 shells.  These are selection gates; the displayed reserve maxima remain the native shells/nails capacities.
- `hwjam2` reuses the AD profile and AD held/preview upgrade resolution.  It does not duplicate those entries.
- Snack defaults preserve the reference held and muzzle values: Stakegun (`v_nail`) and Impaler (`v_nail2`) have dedicated one-sixth scale, grip, and muzzle values; Rotary (`v_shot3`) uses the shared Dwell mesh calibration.  They live in the existing shared classic slot path, so solo and co-op retain one authored calibration.
- `SV_CalcStats` reads `items_snack` only when `moditems` is unavailable, after the existing `items_dwell` fallback, and publishes it as `STAT_VR_MODITEMS`.

## Source-origin split

The client-side source correction maps directly to `VR_WeaponCalibrationProjectileSourceOffset` in the owned calibration file:

- Snack Impaler is `IT_SUPER_NAILGUN` with exact held model `progs/v_nail2.mdl`; its native projectile source is `self.origin`, so suppress the generic +8-forward/+16-up client correction.
- Snack Stakegun is `IT_NAILGUN` with exact held model `progs/v_nail.mdl`; use +11 forward instead of the generic +8.  QuakeC retains its alternating +/-2.5 right-barrel offsets.

`Quake/pr_cmds.c` and `Quake/sv_user.c` remain main-owned.  If their server-side VR source compensation has a selector/model switch, translate the same two exact predicates there: Impaler must use self origin; Stakegun must add eleven forward and retain QuakeC's lateral barrel expression.  No generic Snack name or model-pattern rule is appropriate.

## Verification

Run the existing calibration fixture after implementation.  It directly executes the owned projectile-source helper and can prove the two exact source corrections without a full build or game pass.  Menu and server transport remain source-reviewed because this task excludes full builds and game execution.
