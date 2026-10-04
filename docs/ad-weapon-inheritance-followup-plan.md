# Generic AD weapon calibration inheritance

## Verified reference and boundary

The behavioral reference is the existing `vr_ad_weapon_fallbacks` table and the
classic held/muzzle relationship in the legacy `InitAllWeaponCVars`. The local
table already contains the fifteen AD calibrations; no renderer transform or
weapon schema owner needs replacing. Local `BuildPreset` instead chooses AD
using game-directory names, and the copied generic-profile filter is q30-only.
`VR_WeaponCalibrationHasADRoot()` is absent from this checkout. The equivalent
identity boundary stays inside the calibration translation unit.

Read-only installed-asset inspection confirms the fifteen calibrated rooftop
and q30 models are byte-identical to AD's packed models. Fourteen HWJAM2 models
also match. Its older nailgun differs at seven header bytes; all bytes after the
84-byte header match, and maximum decoded vertex differences are below
0.000114 Quake units. Two verified nailgun asset fingerprints therefore share
one existing calibration, without a mod-specific exception. Stock id1 models
with overlapping names differ. Existing
`progs/ad171/` aliases identify reusable AD models, rather than a new mod list.
AD's grappling hook and `v_zershot` have no built-in calibration in this table;
this change must not invent transforms for them.

## Design comparison before implementation

| Approach | Reuse and behavior | State/complexity | Decision |
| --- | --- | --- | --- |
| Add effective-asset identity at calibration reload | Reuses filesystem search, AD rows, `BuildPreset`, schema application, cvar slots, enhanced lookup and save ownership. Each matching model inherits only its own classic AD values. | One bounded cached identity result per existing AD path/alias; no per-frame reads. | Implement. |
| Extend mod-directory allowlists | Reuses rows but guesses geometry from a folder, misses renamed/future forks, and misclassifies stock-name overrides. | Growing duplicate family policy. | Reject. |
| Replace calibration loading or renderer transforms | No demonstrated incompatibility with either component. Would duplicate schema/slot ownership and risk saved/Enhanced behavior. | New policy and state machinery. | Reject. |

## Intended implementation

1. Identify only effective model assets using the engine's filesystem API, which
   already resolves active roots, packs and loose files. Use bounded content
   signatures of verified AD geometry variants; filenames alone never admit a
   model. Cache successes and failures at each game calibration reload.
2. Keep the existing generic/special preset construction. Replace automatic AD
   game-name classification with per-model AD inheritance. Preserve explicit
   preset selection for nonmatching assets and existing authored special
   profiles. Confirmed AD assets always receive their existing AD tuple after
   generic preset rows. Reuse existing AD alias mappings with the same identity
   guard.
3. Generalize the q30 legacy filter only for identified models receiving AD
   defaults: suppress an entire exact generic held-offset/scale/muzzle triple.
   Any changed component, partial tuple, unrelated field, non-AD asset and
   Enhanced calibration stays authored. Schema loading remains the last owner.
   Runtime profile files are never rewritten. Intentional saved values exactly
   equal to the old generic tuple remain indistinguishable from copied defaults.
4. Keep classic and Enhanced/MD5 lookup branches independent. Classic AD
   inheritance must not seed enhanced fields or apply classic scale to MD5.

## Integration proof and constraints

Smallest vertical proof: an unnamed standalone fork with AD shotgun bytes and
a copied generic profile reloads to the built-in AD held, muzzle and scale;
changing a saved component preserves that authored tuple. A mixed stock/AD
root must retain stock values on the nonmatching weapon.

Prepare `tests/vr_ad_calibration_fixture.c` for effective file priority,
identity rejection, independent models, authored offsets, alias handling,
reload invalidation and absence of lookup-time IO. Include read-only installed
AD/rooftop/HWJAM2/q30 assets supplied through a read-only base-directory argument,
without committing any game assets. Hold fixture execution and all builds until main integration as
requested. Then run focused calibration/schema/save checks and verify held and
muzzle alignment in the relevant maps using the existing rendering/input path.
No commit, push, deploy, platform build or runtime-file write is authorized.

## Implemented adapter and evidence

`VR_WeaponCalibrationRefreshADIdentity` opens the fifteen canonical paths and
thirteen existing aliases through `COM_OpenFile` on each game calibration
reload. The sixteen length/FNV-1a32 signatures cover both nailgun versions.
Only exact candidate lengths are read, so each allocation/read is at most
97,860 bytes; no model-format parser or new filesystem owner is introduced.
Both successful and unsuccessful identity results are retained until reload.
`BuildPreset` uses them to append the complete classic AD held/scale/muzzle
rows, and the legacy filter checks that AD inheritance was actually eligible.
Special authored profiles retain their original branches. Enhanced lookups
retain their own offsets and neutral scale, independent of classic identity.

Read-only asset evidence matches the source signatures for all fifteen
calibrated weapons in each of AD, rooftop, HWJAM2 and q30. Native search priority
is resolved by `COM_FindFile`, not by scanning a named AD donor directory:
higher active game roots precede lower ones; within a root, higher numbered
contiguous packs precede lower numbered packs, which precede loose files.
The installed AD and rooftop weapons are packed; HWJAM2 and q30 are loose.
The fixture mounts those native search nodes while deliberately giving
calibration an unrelated `com_gamedir`, then exercises production file handles,
memory packs, schema parsing and public held/muzzle lookup.

The following recipe is prepared for main integration, **not executed here**:

```sh
cc -std=gnu11 -DUSE_SDL3 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -fno-sanitize-recover=all -fno-omit-frame-pointer -IQuake \
  tests/vr_ad_calibration_fixture.c Quake/vr_weapon_calibration.c \
  Quake/vr_weapon_schema.c Quake/vr_locomotion.c Quake/mathlib.c \
  Quake/common.c Quake/sys_sdl.c \
  -Wl,--gc-sections -Wl,--wrap=COM_LoadFile -Wl,--wrap=Sys_FileRead \
  $(pkg-config --cflags --libs sdl3) -lm -o /tmp/vr-ad-calibration-fixture
/tmp/vr-ad-calibration-fixture "$AD_FIXTURE_BASEDIR"
```

Existing isolated calibration fixtures now also need filesystem/hash linkage
or narrow stubs. Their game-name-only AD assertions must be supplied with real
identified assets at main integration; those files are outside this write set.
Source review and whitespace checks are the only code checks performed here.
Compilation, fixture execution and rendered VR alignment remain pending.

## Limits and follow-up

Full-byte identity intentionally fails closed on unrecognized AD versions,
reskins or rewritten exports, even if some geometry happens to be equivalent.
Add only verified asset signatures sharing the calibration, or author a schema
for different geometry. Existing aliases cover only the already mapped paths;
arbitrarily renamed viewmodels are not discovered. FNV-1a32 plus exact length is
a practical asset identity, not an adversarial collision-proof digest.
Filesystem changes take effect at calibration reload. Explicitly selecting the
AD preset retains its existing manual meaning for unrecognized assets.

An intentional saved tuple equal to all generic defaults cannot be distinguished
from the copied legacy tuple; every changed or partial tuple stays authored.
AD's uncalibrated extra assets and the absent `HasADRoot` helper do not justify
editing model loading, menus, input, audio or filesystem code. Final qualification
must inspect held scale and muzzle alignment on the three reported map packs.
