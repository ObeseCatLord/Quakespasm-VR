# Installed AD calibration audit — 2026-10-04

Scope done: audited the read-only `quakespasm_straight` installation and added
four verified signatures on branch `2.0`. Production loading, calibration
tuples, authored schema precedence and special presets retain their existing
owners. No asset/config edits, commits, deployment or renderer changes.

## Effective filesystem and coverage

Surveyed every immediate directory plus nested directories containing
`pak0.pak` or `progs.dat`. Resolved each gameplay root above `id1`, walking
contiguous numbered packs exactly as `COM_AddGameDirectoryRoot` does. Higher
numbered packs precede lower packs and loose files; the first duplicate entry
within a pack wins. No numbered-pack gaps were found. ZIP downloads and loose
files shadowed by mounted packs were not treated as effective models.

The usable inventory contains 38 top-level gameplay roots plus
`dwellv2p2/dwellv2p2`: 13,540 effective `progs/*.mdl` instances, 4,673 unique
contents, including 801 viewmodel instances. Compared every effective MDL's
content against the existing 16 fingerprints, including arbitrary model paths;
then screened all 4,657 unmatched unique models against the fifteen AD donors.
The offline MDL comparison consumed complete files and compared triangle
topology, ordered animation poses, frame grouping/intervals, decoded positions,
normals and flags. Ignored trailing data was allowed after complete MDL frame
streams, matching `Mod_LoadAliasModel`. Four unrelated non-viewmodels were
excluded from complete decoding: Mjolnir's RAPO/v50 `madfox/h_demon2.mdl`, and
unhandled skin-group data in `madfox/megaweb.mdl`, Rooftop's `qrt_vines5.mdl`
and `rooftops_trees5.mdl`. The latter three have vertex/triangle/frame counts
unlike every calibrated donor. The other 4,653 unmatched models were decoded.
This is audit tooling, not a production identity parser.

Canonical calibrated paths are `progs/v_shadaxe{0..5}.mdl`, `v_shot{,2,3}.mdl`,
`v_nail{,2}.mdl`, `v_rock{,2}.mdl`, `v_light.mdl` and `v_plasma.mdl`.
Counts below describe recognized assets, before special-preset/schema policy.

| Root | Effective calibrated files / source | Matches before → after |
| --- | --- | --- |
| ad | All fifteen; pak0 | 15 → 15 |
| gibtropolis | All fifteen; pak0 | 15 → 15 |
| hwjam4 | All fifteen; pak0 beneath pak2/pak1 | 15 → 15 |
| quake_rooftop_jam_v2 | All fifteen; pak0 | 15 → 15 |
| hwjam2 | All fifteen; loose, older nailgun | 15 → 15 |
| q30a1024 | All fifteen; loose | 15 → 15 |
| mjolnir | Fourteen `progs/ad171/` aliases; loose | 14 → 14 |
| ravenkeep | Axes 0–5, shot3, nail, rock, rock2, plasma; loose | 11 → 11 |
| peril3.0 | shadaxe0; pak0; higher packs replace other weapons | 1 → 1 |
| reliq | light, rock; adds shot2, nail, nail2, rock2; pak0 | 2 → 6 |
| limjam | light; adds nail; pak0 | 1 → 2 |
| alk | light; adds nail; pak0; special preset retained | 1 → 2 |
| dwellv2p2 | shot, rock, rock2, light; adds nail; loose | 4 → 5 |
| dwellv2p2/dwellv2p2 | Same five; loose above its own pak0 | 4 → 5 |

The other 25 roots have zero recognized calibrated AD assets:
Tershibboleth, bonkjam, ctsj2, dopa, enyo, hipnotic, honey, id1, immortal,
mg1, mg3, nyarlathotep, qbj3, qdoom, rm1.2, rogue, sacrilege, smej2, snack3,
something_wicked, spiritworld, tombofthunder, udob, vr and warpspasm.
The native fixture checks both matching and rejected canonical/alias paths
across all 39 roots, with an unrelated game name for identity tests.

## Verified misses and minimal fix

| Canonical path | Bytes / FNV-1a32 | Installed occurrences |
| --- | --- | --- |
| progs/v_shot2.mdl | 131164 / 7242f947 | reliq/pak0 |
| progs/v_nail.mdl | 47140 / d3d8b117 | alk/pak0, limjam/pak0, reliq/pak0; both Dwell installs loose |
| progs/v_nail2.mdl | 48964 / c79ffca6 | reliq/pak0 |
| progs/v_rock2.mdl | 39668 / 18f7a730 | reliq/pak0 |

All four preserve donor triangle topology, animation ordering/timing, normals and
flags. Nailgun reskin changes skin/UV bytes and the already accepted older
header float encoding: maximum decoded coordinate difference across all nine
poses is 0.00011399388313293457 Quake units. Super nailgun changes only skin
bytes; rocket launcher changes skin/UV bytes. Both have zero coordinate
difference. Reliq's shot2 reuses AD shot3's 206 vertices, 238 triangles and seven
poses; both fallback rows have the same tuple. Its structured bytes after the
skin match the donor; rounded header floats change coordinates by at most
0.00003147125244140625 units, and 53,296 trailing bytes are ignored by the
engine's frame reader. No parser change is needed. SHA-256 fingerprints for
shot2, nail, nail2 and rock2, respectively:

```text
fe6439a01a4e1e82668c77a795e7d890806d8015af1cbcdde59cd9b982851939
60fcfb73839519938b8176dda6ec4920d8700ef87df9e3a6cfdcfd7f96ed48a6
98a239b4fcfcf78c68079de14984bb8c30b251418044f63c9097c5c16f64da09
68e38e54cf4aad130c6b3ad2d8ce12ba20e4ffcd4c5a866aae85b69e56ce50c3
```

The launcher reskin also occurs as `progs/v_rifle.mdl` in both Dwell installs.
Those paths already have authored special calibrations; no global alias was
added. Four signature rows reuse the existing bounded reload cache, AD
profiles and generic-copy filter. A parser rewrite would add runtime complexity
without admitting any further demonstrated compatible model.
The largest accepted file/read is now 131,164 bytes; allocation remains bounded
by exact table lengths.

Mjolnir's unrecognized `ad171/v_shot2.mdl` has 94 vertices/86 triangles versus
the calibrated donor's 106/116; it cannot receive that tuple merely by name.
Ravenkeep's other four canonical replacements and Peril's fourteen replacements
also fail the geometry comparison. Their existing authored profiles remain
authoritative. AD's spike/hook/zershot and unrelated models lack calibrated AD
donors; no transforms were invented.

## Verification and limits

Built the expanded fixture using the full compiler recipe in
`ad-weapon-inheritance-followup-plan.md`: warnings as errors, AddressSanitizer,
UndefinedBehaviorSanitizer, production `common.c`/`sys_sdl.c`, and the existing
file-read/load wrappers. Executed:

```sh
/tmp/vr-ad-calibration-fixture /home/obesecatlord/Windows/Games/quakespasm_straight
```

Passed all 39 root matrices and effective installed `vr_weapons.txt` loads via
production `COM_LoadFile`. Verified authored classic/Enhanced fields survive,
exact complete generic copies receive AD values, and changed/partial tuples
remain authored. Each new reskin passes all seven independently changed tuple
components and rejects a private-memory byte mutation (including the ignored
shotgun trailer). Packed provenance, stock override priority, alias guards,
short reads, reload invalidation and
absence of lookup-time IO also pass. `git diff --check` passes.

`dwell` and `mjolnir1.0` contain profiles but no gameplay assets; `thundergoose`
contains a loose map rather than a mountable model root. Backup/source-only
`progs.dat` copies were inspected and excluded from playable counts. Arbitrary
extra base directories, runtime mount combinations and future asset versions
are outside this standalone `id1 + selected root` inventory. Exact length/FNV
identity retains its existing collision and unknown-export limitations. An
intentional tuple exactly equal to generic defaults remains indistinguishable
from a copied default. Rendered VR held/muzzle alignment remains main's
follow-up; this audit verifies native resolution and public calibration output.
