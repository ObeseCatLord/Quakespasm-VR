# Peril 3.0 weapon wheel audit

2026-10-04, branch 2.0. Implementation decisions recorded before production edits.
Scope: existing wheel profile/catalog/ownership/icon seams, Peril fixtures, this
audit. Installed assets/configuration are read-only. No builds, tests, runtime
launches, commits, deployment or nested agents during this implementation.

## Native evidence and roster

Read every numbered installed pack: pak0 (1339 members), pak1 (785), pak2 (367),
pak3 (845); all contiguous, no higher numbered pack or loose progs.dat override.
pak0 has the old program; pak2 has the effective program; pak3 has none.
Effective pak2:progs.dat is 2347206 bytes, SHA-256
`5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`.
All twenty viewmodels and all twelve active pickup models resolve to pak0.
No pack contains wwheel.txt or vr_weapons.txt. The loose vr_weapons.txt contains
stock/mission-pack calibration triples and viewmodel names, no selector/impulse
or ownership declarations. It is calibration input, not a weapon roster.

Source below is the **effective pak2 my_progs** source. Compiled globals agree
with the selectors, modifiers, capacities and all twelve held/pickup paths.
W_SetCurrentAmmo: function 1340, statement 70260, parm_start 20487, one entity
parameter. W_ChangeWeapon: function 1344, statement 70616, parm_start 20490,
one entity parameter. Native moditems is float field 320; items is field 58.

| Native pickup label (uppercase in wheel) | items selector | impulse | Ammo/minimum to select | Pickup / held (progs/*.mdl) |
| --- | ---: | ---: | --- | --- |
| Quake Axe | 4096 | 1 | none | g_axe / v_shadaxe0 |
| Sawn-off Shotgun | 1 | 2 | shells / 1 | g_shot1 / v_shot |
| Double-barrelled Shotgun | 2 | 3 | shells / 2 | g_shot2 / v_shot2 |
| Nailgun | 4 | 4 | nails / 1 | g_nail / v_nail |
| Super Nailgun | 8 | 5 | nails / 2 | g_nail2 / v_nail2 |
| Grenade Launcher | 16 | 6 | rockets / 1 | g_rock / v_rock |
| Rocket Launcher | 32 | 7 | rockets / 1 | g_rock2 / v_rock2 |
| Thunderbolt | 64 | 8 | cells / 1 | g_light / v_light |

Native labels: items.qc:1525,1560,1610,1647,1684,1721,1758,1806. The paired
SMG mesh remains named **Nailgun** in native QC; do not relabel it by appearance.
The sword-shaped mesh remains **Quake Axe**, not an invented Sword selector.
defs.qc:269-278 supplies stock item bits. weapons.qc:1028-1087 owns selection;
settings.qc:303-305 dispatches impulses 1..8 to it.

| Replacement label | moditems mask | Parent selector / impulse | Pickup / held |
| --- | ---: | --- | --- |
| Shadow Axe | 4096 | 4096 / 1 | g_shadaxe / v_shadaxe3 |
| Grapple Hook | 128 | 4096 / 1 | g_ghook / v_ghook |
| Widowmaker Shotgun | 2 | 2 / 3 | g_shot3 / v_shot3 |
| Plasma Gun | 64 | 64 / 8 | g_plasma / v_plasma |

defscustom.qc:663-666 defines modifiers, :1667-1694 defines models;
items.qc:1377,1411,1449,1487 defines upgrade labels. weapons.qc:873-925
chooses models; grapple takes precedence over Shadow Axe if both bits exist.
items.qc:1090-1120 removes the opposite melee upgrade on pickup, :1310-1318
adds the parent items bit and modifier. These are **eight native slots / twelve
active held identities**, not twelve independently selectable weapons.
moditems alone is not ownership: W_ChangeWeapon requires the parent items bit.

defscustom.qc:1429-1432: capacities shells/nails/rockets/cells = 200/200/100/100.
Widowmaker requires only two shells to select: weapons.qc:375-390 fires a
reduced two-shell shot if fewer than three remain. Do not require three.
Existing active-entry selectability is preserved (releasing the active entry
does not send a weapon-change impulse); native QC still owns ammo exhaustion.

## Minimal adapter decisions

Use eight explicit Peril profile rows in the current catalog; retain stable
stock slot IDs, native items ownership and STAT_ACTIVEWEAPON. Share the existing
AD upgrade viewmodel/preview rules. Peril model matching accepts only its twelve
active held paths, not the dormant shadaxe1/2/4/5 aliases accepted by generic AD.
Use native labels including dynamic replacement labels. Label/icon accessors
are descriptor seams; do not change session, input, hover/release or draw lifecycle.

Peril is a complete native profile, so suppress stock/mission-pack fallback
entries; skip Hipnotic/Rogue builtin additions for this game. Preserve existing
wwheel and real wheel-schema declaration precedence. In Peril, schema entries
whose only wheel key is viewmodel are calibration-only: applying them to the
wheel would pin v_shot2/v_light and disable native upgrades. Skip those in the
wheel consumer, leave calibration parsing/authoring untouched. Authored wheel
keys (selector, impulse, preview, ownership, etc.) continue through the normal
schema merger. No parallel catalog or invented inventory bits.

Native stat producer already reads moditems into STAT_VR_MODITEMS (231) at
Quake/sv_main.c:1115-1118 and global ammo capacities at :1009-1021. Ownership
reads items, not an invented weapons field absent from this program.

## Exact desktop icon decisions

Sbar_WeaponMenuIcon at Quake/sbar.c:302-313 actually returns only the seven
stock sb_weapons[0] icons (selector 1..64), and NULL for axe/unknown selectors;
it does not return Hipnotic/Rogue icon banks. Sbar_LoadPics :176-182 loads
inv_shotgun, inv_sshotgun, inv_nailgun, inv_snailgun, inv_rlaunch,
inv_srlaunch, inv_lightng through gfx.wad. W_LoadWadFile :66-86 loads the
effective gfx.wad. Peril pak0..pak3 and loose assets supply no gfx.wad or
replacement inv_* artwork: these wheel pictures are inherited id1 pictures.

Peril's supplied CSQC vanilla HUD explicitly shares those seven conventional
names (csqc_hudvanilla.qc:117-127). Its AD HUD supplies distinct Widowmaker and
plasma identities (csqc_hudad.qc:665-675), but Sbar_WeaponMenuIcon cannot access
those identities. A selector-only call would depict SSG for Widowmaker and
lightning for Plasma Gun. Those upgrades get label-only desktop fallback.
The paired v_nail/g_nail SMGs also get label-only fallback: inherited inv_nailgun
depicts the stock single nailgun, not two SMGs. All melee variants remain
label-only because the inherited API has no axe icon. The remaining six native
base firearm categories can share their native vanilla-HUD category icons;
these are not claimed as exact reproductions of Peril's replacement meshes.
An explicitly authored Peril model/viewmodel also falls back conservatively to
label-only, avoiding an unverified selector/art match.
No generated art, atlas extraction renderer, textures or new rendering path.

## Calibration audit

Quake/vr_weapon_calibration.c:564-585 already covers every packaged viewmodel,
including all twelve active identities and eight dormant files. :3008-3010
selects the Peril preset; :3115 onward filters exact complete copied generic
triples while preserving changed/partial authoring. Existing
tests/peril_weapon_landmarks_fixture.h independently records decoded model
grip/muzzle landmarks. Retain all numeric geometry-derived defaults and authored
offsets. No demonstrated missing model or calibration row; no calibration
production edit is justified. Headset ergonomics and moving animation remain
qualification work; SNG remains a single gun.

## Implementation / qualification checkpoint

Production edits are stable in Quake/vr_weapon_menu.c. The eight profile rows
retain stock stable IDs and share AD model switching. Offset-only schema rows
are ignored only by the Peril wheel consumer; explicit wheel keys still merge.
Peril also excludes mission-pack builtins even when their legacy flags are set.
Native labels are used for both diagnostics and desktop presentation.

Integration review caught a prepared-descriptor bug: re-resolving labels/icons
from mutable stats during DrawCatalog would disagree with prepared hit boxes.
The existing visible descriptor now stores resolved label/icon fields;
BuildVisible resolves them against its supplied stats. PrepareFrame copies the
label into its existing owned frame label array; the icon pointer is the same
borrowed Sbar picture resource as before. Geometry and draw consume these
prepared fields. Custom DrawCatalog fallback resolves its own supplied stats.
This correction adds two descriptor fields and a few assignments/accesses;
there is no extra catalog, upgrade state, input state or draw lifecycle.

Additional icon evidence: effective pak0 has nine `gfx/adw_*.lmp` and matching
`gfx/adws_*.lmp` pictures. csqc_hudad.qc:194-199 names those paths, including
adw_widowm.lmp and adw_plasma.lmp. Read-only visual inspection shows the latter
two add WM/PG lettering to conventional gun silhouettes; adw_nailgun.lmp still
depicts a single stock nailgun. They are separate CSQC pictures, not replacement
gfx.wad lumps reachable through Sbar_WeaponMenuIcon. Label-only replacement
fallback avoids adding a new picture loader or claiming exact replacement art.

Added, **not executed**:

- tests/peril_weapon_wheel_fixture.c: actual native VFS/loose calibration file,
  eight slots under all Hipnotic/Rogue flag combinations, twelve active held
  models, upgrades/inverse, hook priority, parent ownership, zero/one/two ammo,
  release revalidation, schema/wwheel overrides and desktop icon routing.
  Includes an independent snapshot oracle: prepare upgraded descriptors, mutate
  live stats, call production DrawCatalog with recording GPU primitive stubs,
  assert expected labels and four icons (live resampling would emit base labels
  and six icons). Custom-catalog drawing uses alternate stats.
- tests/peril_weapon_wheel_runtime.gdb / .sh: actual absolute pointer/hover and
  release, native W_WeaponFrame -> ImpulseCommands -> W_ChangeWeapon, all eight
  selectors, each upgrade and inverse, simultaneous melee flags, parent gate,
  SSG/SNG two-unit minimum, two-shell Widowmaker, unchanged selection ammo,
  no duplicate observation rows and explicit schema command/path/ammo precedence.
  Seeds inventory/modifier boundary states; does not claim pickup acquisition
  or physical mouse/button/focus qualification.
- Runtime stat proof calls SV_CalcStats, actual SVFTE_WriteStats encoding and
  actual CL_ParseServerMessage decoding. Asserts native items, moditems=231,
  held model index, active selector, ammo and 200/200/100/100 capacities on the
  receiving client. Uses existing FTE replacement-delta/predinfo extensions;
  this is an isolated in-process wire roundtrip, not a live peer or negotiated
  connection. No guessed fields or packet format.
- tests/run_peril_weapon_wheel_native.py: builds the scoped policy fixture with
  ASan/UBSan, then runs the GDB qualification against main's integrated debug
  binary. Shell wrapper links installed packs/maps/calibration input into a
  temporary game tree. It does not link installed writable configuration files.

Deferred commands, only after main integrates all workers and authorizes testing:

```sh
python3 tests/run_peril_weapon_wheel_native.py --binary /path/to/integrated/vkquake \
  --output-dir /tmp/peril-wheel-results
python3 tests/run_peril_weapons_native.py --binary /path/to/integrated/vkquake \
  --output-dir /tmp/peril-calibration-results
```

Verification performed this turn: read-only plans/source/diff inspection,
all numbered PAK directory/member resolution, effective program hash and
compiled globals/function/field inspection, calibration coverage inspection and
native HUD picture inspection. No test, build, fixture execution or game launch
was performed. Fixtures remain uncompiled/unqualified and may require harness
fixes after the integration checkpoint. No installed asset/config changes,
commits, deployment, nested agents or modifications outside the owned scope.

Follow-up: main runs integrated build/fixtures and Astra review; desktop input
worker qualifies mouse/camera/focus/capture behavior. Connected desktop Peril
must show the eight owned slots, change correctly after release and react to
upgrades/inverse. Actual graphics and physical VR calibration comfort remain
user-visible qualification. All twelve active held/pickup paths exist; no native
selector or packaged calibration row remains unidentified by this audit.

## Integrated qualification

The worker checkpoint above is historical. Main's final integrated ASan/UBSan,
actual QC/stat transport, paired firing and graphical desktop checks passed.
[Results and limits](peril-desktop-wheel-2.0-results.md) include the private
mouse-to-QC proof and inspected eight-slot Peril screen-space wheel.
