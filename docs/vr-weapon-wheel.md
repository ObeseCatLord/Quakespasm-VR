# Weapon-wheel definitions

The wheel lists owned weapons, including empty weapons. An empty reserve can
prevent selection, but it must not hide the weapon. Known engine profiles and
declared weapons do not need to be equipped first. A mod change rebuilds the
catalogue; a map change invalidates map-local model indices without discarding
known definitions or user calibration.

## Calibration is not an inventory declaration

`vr_weapons.txt` may contain only held-model scale, grip, or muzzle offsets.
Those values calibrate rendering/aim; they do not establish which inventory
field owns a weapon or which QuakeC impulse selects it. Existing verified
profiles supply those facts for supported custom weapon families.

For a new weapon, provide its native selector, command, ownership and active
stats. For example, this *hypothetical* mod stores ownership in `.weapons`,
uses bit 256 for its chainsaw, and selects it using impulse 226:

```text
{
    bitmask 256
    impulse 226
    owned_stat weapons
    owned_mask 256
    active_stat activeweapon
    active_mask 256
    model progs/g_saw.mdl
    viewmodel progs/v_saw.mdl
    scale 1
    offset 0 0 0
}
```

These numbers must come from the mod's game code, not from its model names.
`model` is the wheel preview; `viewmodel` is the held-model identity. Different
names are valid. `scale` and `offset` affect the wheel; `held_scale`,
`held_offset`, and muzzle offsets are separate calibration fields.

Supported stat names include `items`, `activeweapon`, `weapons`, `items2`,
`moditems`, `weapon2`, `weapons2`, `ammo`, `shells`, `nails`, `rockets`, and
`cells`; numeric client-stat indices are also supported. Optional `ammo` or
`ammo_stat` and `ammo_max` describe ammunition. Runtime capacity extensions
take precedence over a fixed maximum. Magazine-only `ammo` is not a reliable
empty-reserve test for an inactive weapon; selection remains subject to the
mod's own rules.

## Complete rosters and partial overrides

By default, file entries enrich matching definitions. Omitted command,
ownership, ammo, model, scale, and offset fields do not erase known values.
A mod-provided `wwheel.txt` supplies its native roster, but does not overwrite
explicit `vr_weapons.txt` fields or wheel geometry. An inherited base-game
roster must not define a different mod's weapon inventory.

When a mod's `vr_weapons.txt` declares its **entire** custom roster, add this
outside the weapon blocks:

```text
roster complete
```

This suppresses undeclared generic stock guesses. Do not use it in a file
containing only offsets or one command override. The declaration applies only
to the active game's own file. Verified complete built-in profiles also
suppress unrelated stock guesses.

Two entries with conflicting explicit ownership or active descriptors are
different identities even if they share a selector or model. An ambiguous
overlay is diagnosed and skipped, not applied to whichever row happens to
come first.

## Upgrades and discovery

An upgrade that replaces a native weapon slot should replace its preview,
not become another independently selectable entry. AD's shadow axe/grapple,
Widowmaker, and plasma upgrade, and Enyo's AV72 upgrade, use this rule. Their
native parent weapon owns the slot; an upgrade flag alone is not ownership.
HWJAM2 uses the same AD upgrade slots. Snack Pack's Rotary Shotgun instead
has separate ownership in `items_snack` (exported as `moditems`), not the
stock armor bit with the same numeric value. Its native shotgun toggle is
used with target-aware retries to select the double or rotary shotgun.

Snack Pack's Impaler uses a dedicated one-sixth held scale, a centered rear
grip and a bolt-exit muzzle. Its native projectile launches from `self.origin`
and retains the mod's lobbed trajectory. The Stakegun also has a dedicated
grip/muzzle calibration and preserves its native alternating barrel origins.
The Rotary shares Dwell's calibrated mesh profile; HWJAM2 shares AD's profiles.
Old engine-generated generic calibrations are upgraded only when the classic
scale, grip and muzzle all still match their old defaults. Personal,
multiplayer, enhanced and global calibrations are preserved.

Wheel previews use their own scale, independent of held calibration, and
are centered on all three axes at their selection point, including when
highlighted or rotated. A pickup model is preferred; a missing pickup falls
back to the held model.

Runtime discovery records genuinely new held models and selectors. It does
not guess arbitrary selection impulses or treat coincident key, armor,
upgrade, or secondary-active bits as inventory. For an unfamiliar mod with
nonstandard ownership/commands, add explicit declarations. Filenames alone
cannot guarantee correct selection for arbitrary QuakeC.

Use `vr_weaponlist` to inspect catalogue sources, visibility reasons,
selectors, commands, and ownership metadata when diagnosing a mod.
