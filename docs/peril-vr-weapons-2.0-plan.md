# Peril 3.0 VR weapons: incremental plan

Scope: branch `2.0`; calibration, the existing MDL splitter, input, and existing
model recipes/mod compatibility adapters. Physics movement, codec changes and slope/solid fixtures remain
owned by other workers. Subsequent permission grants cover only the existing
server weapon-pose, builtin makevectors/aim and capability-offer regions, plus
Peril regression fixtures and the exact Peril calibration-audit case. No installed assets/configs, deployment, commits or
nested agents.

1. Inventory the effective installed PAK contents, viewmodels and compiled QC.
   Measure frame bounds/topology and inspect firing functions; compare existing
   installed offsets and the read-only inherited implementation with QBJ3.
2. Add a Peril-specific built-in calibration table covering every actual weapon,
   preserving external calibration precedence. Derive paired offsets from the
   single retained gun/hand rather than the intact two-gun model.
3. Extend the current pinned splitter and `gl_model.c` recipe table only for
   demonstrated separable Peril geometry. Reuse current paired renderer/input.
   Extend the existing QC compatibility adapter only if the installed program
   demonstrates reliable per-hand shot selection/source compensation.
4. Run focused existing fixtures and an isolated build; use installed QC/assets
   read-only in temporary fixtures. Document inventory, measurements, exact
   program/model identities, results and remaining user-visible qualification.

Architecture: an incremental recipe/calibration/QC adapter preserves the existing
single owner for geometry, animation, weapon policy, wire poses and firing.
A separate akimbo subsystem would duplicate those policies and is unnecessary.
The smallest vertical proof is two separated Peril guns with native QC shots
originating/directed from the correct hand while ammo/cadence remain native.
The initial plan required reporting server hooks to main before editing them.
Main subsequently authorized the exact weapon regions in `sv_phys.c`,
`pr_cmds.c` and the akimbo offer in `sv_main.c`; movement and codec regions
remain outside this worker's write set.
Missing source or unsupported bytecode is a blocker, not grounds for simulated
akimbo or an unverified whole-model duplicate.

## Installed evidence and calibration

The effective program is `pak2.pak:progs.dat`, 2,347,206 bytes, SHA-256
`5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`.
The contiguous installed pak0..pak3 override order was re-audited; pak0 also
contains an older program, while pak3 contains no progs.dat and there is no
loose override. The isolated native engine loaded this exact pak2 hash. Main
confirmed that its deliberately named q30 predicate recognizes this same
shared AD QC; that name does not indicate an installed-asset collision.
Weapon source is supplied in the same pack: `my_progs/defscustom.qc:1683-1694`
names the twelve active viewmodels; `my_progs/weapons.qc:874-925` selects them.
All twenty packaged viewmodels are in `pak0.pak`; dormant means not named by
this program's viewmodel constants, not another invented weapon slot.

Measurements decode ready-frame vertices as byte*scale+origin, inspect connected
triangle components and all frame records, and visually inspect a static mesh
plot. Existing installed generic triples and inherited AD/QBJ3 presets were
checked first. Peril's sword and reskinned gun headers differ enough that those
triples are not the correct origin-aware offsets. Grip landmarks below are
chosen inside the modeled palm/handle region; these are initial geometry-derived
presets, not a claim of completed headset ergonomics or animation qualification.
Scale retains the existing half-size gun/quarter-size melee convention. Each
held row is `-s*grip-(1-s)*origin`, matching `R_AliasModelMatrixInternal`.
The SMG Y offset centers the average mirrored grip, leaving at most 0.352
world units of lateral ready-pose residual at default world scale. Ordinary
muzzle coordinates use the default `vr_world_scale=1`, `vr_gunmodelpitch=0`,
`vr_gunmodely=0`: transform the muzzle with the same draw matrix and convert
forward/left/up to right/up/forward. Custom world scale, global height/pitch,
recoil, and authored replacement assets need independent calibration/QA.
Paired input instead transforms decoded barrel anchors through the actual draw
matrix, reusing existing QBJ3 logic; no new input implementation is necessary.

| Viewmodel (`progs/`, `.mdl`) | Native use | Bytes / CRC32 | Vertices / triangles / frames | Ready grip (forward, left, up) | Scale |
| --- | --- | --- | --- | --- | --- |
| `v_shadaxe0` | active | 97860 / `5afd327a` | 281 / 292 / 21 | 31, -16.7, -16.5 | 0.25 |
| `v_shadaxe3` | active | 308932 / `271dad35` | 676 / 810 / 21 | 21, -26.5, -29 | 0.25 |
| `v_ghook` | active | 66772 / `20523eb6` | 167 / 261 / 4 | 21, 0, -17 | 0.5 |
| `v_shot` | active | 84644 / `9ad7363c` | 305 / 258 / 9 | 19, 1, -12.5 | 0.5 |
| `v_shot2` | active | 162086 / `80948b40` | 176 / 298 / 14 | 20, -1, -14 | 0.5 |
| `v_shot3` | active | 211216 / `edc30ce6` | 167 / 272 / 42 | 19, -1, -14.5 | 0.5 |
| `v_nail` | active | 84020 / `5ea01698` | 292 / 258 / 9 | 22.39, -11.774, -15.675 | 0.5 |
| `v_nail2` | active | 83476 / `9b906d83` | 345 / 499 / 8 | 22, -8, -16 | 0.5 |
| `v_rock` | active | 58548 / `56d017cd` | 31 / 54 / 7 | 11, -3, -12 | 0.5 |
| `v_rock2` | active | 123396 / `cffc9401` | 383 / 487 / 7 | 17, -7, -11 | 0.5 |
| `v_light` | active | 80004 / `015d25c3` | 310 / 270 / 5 | 24, -5, -18 | 0.5 |
| `v_plasma` | active | 87604 / `6bd4bdb4` | 484 / 397 / 5 | 22, 0, -14 | 0.5 |
| `v_axe` | dormant | 55820 / `3ab096e1` | 146 / 217 / 9 | 25, -10, -25 | 0.33 |
| `v_nail3` | dormant | 97300 / `778d8423` | 516 / 416 / 9 | 27, -8, -16 | 0.4 |
| `v_rock3` | dormant | 213467 / `8e5229a6` | 120 / 208 / 8 | 19, 0, -15 | 0.5 |
| `v_zershot` | dormant | 62604 / `eb439d12` | 94 / 160 / 7 | 12, 0, -14 | 0.5 |
| `v_shadaxe1` | dormant | 308932 / `271dad35` | 676 / 810 / 21 | 21, -26.5, -29 | 0.25 |
| `v_shadaxe2` | dormant | 308932 / `271dad35` | 676 / 810 / 21 | 21, -26.5, -29 | 0.25 |
| `v_shadaxe4` | dormant | 308932 / `271dad35` | 676 / 810 / 21 | 21, -26.5, -29 | 0.25 |
| `v_shadaxe5` | dormant | 308932 / `271dad35` | 676 / 810 / 21 | 21, -26.5, -29 | 0.25 |

The new table covers all twenty models and preserves changed/partial external
calibration. As for AD inheritance, only a complete exact copied stock
held/scale/muzzle triple is filtered. An intentional custom triple identical to
that old stock triple is indistinguishable and will be filtered. Models absent
from the Peril pack but mentioned by the copied installed mission-pack profile
are still external identities; this patch does not invent Peril weapons for them.

## Paired SMG geometry and implemented native firing adapter

`v_nail.mdl` is two SMGs/arms: left vertices 0..135 plus flash 272..281;
right 136..271 plus flash 282..291. The splitter pins size/CRC and verifies
no connected component or triangle spans those partitions. Right-flash vertex
288 crosses Y=0 in frame 3, so sign classification across all frames would
incorrectly reject this model. Each output has 146 vertices, 129 triangles,
nine frames and 74,948 bytes; CRC32 left `0ca1aa33`, right `be17c930`.
All source vertices/triangles, skin pixels/UVs, names and animation records are
retained exactly once; frame bounds are recomputed for each half. Existing
loader allocator, VFS, embedded texture source and enhancement policies apply.
Decoded barrel anchors come from ready-frame end-ring vertices 116..125 (left)
and 252..261 (right): `(39.18381116,7.23141697,-6.20151462)` and
`(39.00579011,-8.25890088,-5.84848383)`.

`v_nail2.mdl` is one rotating-barrel SNG with one arm, not two independently
held guns. Native `snail_upd` uses rotating barrel offsets, including zero.
There is no evidence for proper separated SNG akimbo; duplicating this intact
model and assigning its alternating offsets to controllers would fabricate a
second weapon. Its eight model frames versus native weaponframe 1..8 also
warrants animation QA (the engine's ordinary fallback owns out-of-range frames).

Implemented after main granted disjoint weapon-symbol ownership:

1. In `sv_phys.c`, added a SHA/size/gamedir-pinned Peril program predicate and
   included it in existing `SV_BeginPrivateVRWeaponPose` paired-pose admission.
   Reuse `SV_AkimboCommandValid`, `SV_FindPrivateVRWeaponPose`, body-relative
   command muzzles, relocation invalidation, freshness, and outer restoration.
2. Added the existing-style native makevectors adapter, called by `PF_makevectors`
   in `pr_cmds.c` before ordinary `AngleVectors`. Require the current server VM,
   pinned program, function `W_FireSpikes` (index 1332, first statement 69561,
   parm_start 20408, locals 8, two scalar params), xstatement **69577**,
   player weapon `IT_NAILGUN` (4), model `progs/v_nail.mdl`, weaponframe 1..8,
   valid innermost paired pose and no relocation/invalidation. Read `ox` from
   function-local global **20409**, not OFS_PARM1 (builtins overwrite it).
   Require odd frames `ox=+2` / physical hand 1, even `ox=-2` / hand 0.
   Clamp the selected physical muzzle against the authoritative body anchor,
   set the chosen tracked basis, and compensate entity origin by
   **world `(0,0,16) + ox*v_right`**. Preserve the existing scoped restoration.
3. Added a matching Peril `PF_aim` adapter returning that clamped physical muzzle
   for both initial trace and target correction. This must be the same selected
   pose, not a fresh or older nested pose. Makevectors is mandatory: when
   `autoaim_cvar>=1`, native QC uses `normalize(v_forward*1000)` and never calls
   `aim`. Leave ammo, sounds, damage, rate and `launch_projectile` in native QC.
4. In the authorized `sv_main.c` offer block, enabled the existing `AKIMBO_OFFER_TWIN` / first
   `vr_qbj3_akimbo_protocol` argument for pinned Peril. Native tests below qualify the adapter.
   The new `view.c` compatibility row uses this existing capability and producer;
   no packet/command-layout extension or parallel state machine is required.

Bytecode/source agreement: `my_progs/player.qc:282-296` / statements
71532..71584 set frames 1..8 and ox +/-2 before calling `W_FireSpikes`.
`my_progs/weapons.qc:412-457` / statements 69561..69637 makevectors at 69577,
optionally aim at 69582, and compute `origin+(0,0,16)+v_right*ox` before
`launch_projectile` at 69636 (NG) or 69614 (SNG). Native NG consumes one nail,
SNG two when available, sets attack_finished=time+0.2, and uses speed 1000;
player STATE scheduling is separate and must remain native. This supports an
adapter for the NG/paired SMGs, not an invented SNG hand mapping.

Qualification performed: all eight NG firing frames with opposed controller
yaw and +/-90-degree roll, autoaim branch on/off, native type/speed/ammo/cadence,
body/angle/global-basis restoration, all eight desktop NG and SNG frames,
private SNG exclusion, modified hash, stale pose, one invalid hand, rejected
nested scope, real PF_setorigin relocation invalidation, real BSP floor
clamping and native target-assisted PF_aim correction from the physical muzzle.
No new input protocol or duplicated projectile/damage policy was introduced.
The Peril collision recipe now joins the existing paired collision allowlist.

Astra follow-up qualification uses the current integrated `build-debug/vkquake`.
The existing native harness drives button0 through `W_WeaponFrame`, then runs
ordinary scheduled callbacks through `SV_RunPrivateVRWeaponThink`; it does not
inject firing frames/think callbacks during the lifecycle. Press/hold/release,
repress and ammo exhaustion produce exactly seven shots, frames 1,2,3,4 then
1,2,3, right/left alternation, 0.1-second held cadence, correct first/scheduled
physical muzzles and restored player/basis state. Release emits no extra nail;
exhaustion switches weapons through native QC without negative ammo.
Rejected-shot cases now assert the existing dominant-hand fallback origin as
well as direction. The relocation check first executes successful makevectors
and verifies its physical muzzle cache, then real PF_setorigin invalidates it;
the pinned aim hook rejects without writing stale output and ordinary PF_aim
fallback still works. These checks passed with no production bug or changes.
Evidence: `/tmp/peril-vr-evidence/peril-astra-followup-runtime.log`, markers
`PERIL_INPUT_LIFECYCLE_PASS`, `PERIL_RELOCATION_PASS` and
`PERIL_NATIVE_BOUNDARIES_PASS`. Only the GDB fixture and this qualification text
were changed for the follow-up; no wider physics/network/asset qualification.

Main consolidated the three cross-module declarations in server.h alongside
the existing paired-weapon adapters.

## Verification checkpoint

Passed ASan/UBSan pure split against an independent byte-copy oracle, including
all geometry/frames/skin preservation and corrupt/truncated/wrong-side rejection.
Inherited QBJ3 fixture against this header passes all four byte-identical golden
outputs and all five allocation-failure/rejection cases. Native VFS calibration
fixture passes all twenty rows with the actual installed profile, exact generic
filtering, changed/partial override retention and id1 regression. Independent
numeric landmark calculation verifies all twenty origin-aware grip transforms
and ready muzzle positions under the default draw convention. The existing
stale-akimbo input regression passes with assertion-only stubs for two newer
unexercised melee dependencies; no input source changes were needed.

All seven relevant translation units (calibration, model loader, view, existing
input and the three server files) compiled with the existing debug build's
production warning/error flags into `/tmp/peril-vr-evidence`, then linked with
existing debug objects into an isolated engine. Shared build outputs were not
overwritten; final full integration builds remain main-owned. The production
loader/VFS generation adapter also passes exact half CRCs, source skin identity,
game gating and single-SNG recipe rejection without a GPU.

Persistent regression command (use main's built debug binary for repeat runs):

```sh
python3 tests/run_peril_weapons_native.py --binary /path/to/debug/vkquake \
  --output-dir /tmp/peril-weapons-results
```

The runner reads the effective contiguous PAK viewmodel, generates an independent
byte-copy oracle, builds ASan/UBSan split and native-VFS calibration fixtures,
and drives actual installed QC through the production scoped weapon thinker
in an isolated dedicated process. Both controllers are anatomical hands; native
QC retains its alternating animation/firing schedule and shared attack input.
This implements controller-separated native alternating akimbo, not separate
independent trigger cadences absent from this QC. No mod payload is committed.

Updated only the Peril case in the existing installed calibration audit,
including all twenty landmark-derived profiles and exact copied-generic
filtering. The complete existing installed audit passes under ASan/UBSan;
other mod expectations remain unchanged. The older name-only reload fixture
still fails its absent-AD-profile assertion at line 113 on unchanged HEAD as
well; its asset-free harness cannot qualify current asset-identified AD
inheritance. That pre-existing fixture was not broadened or edited.

Remaining user-visible qualification: headset grip ergonomics, moving/recoil
animation, both handedness settings and texture reload. Numeric presets are
initial ready-pose measurements, assume original Peril assets/default ordinary
muzzle draw settings, and do not claim those headset checks. Single SNG stays
single; its eight model frames versus QC weaponframe 1..8 retains the native
renderer fallback. Main owns full builds; F10 investigation was canceled after the user confirmed
its quit binding is correct. No deployment,
commit, installed configuration/asset write, graphics reset or nested agent.

## Astra final design review disposition

A fresh Astra6/xhigh review independently verified the loaded QC SHA/function
ABI and firing statements, source compensation, calibration arithmetic and
reuse of the scoped thinker. No production blocker or architectural expansion
was identified. Main inspected its load-bearing source claims.

| Recommendation | Disposition |
|---|---|
| Keep the existing adapter; avoid another firing state machine | Adopted |
| Consolidate declarations with existing server interfaces | Adopted in server.h |
| Add normal press/hold/release/ammo-exhaustion scheduling proof | Adopted; native seven-shot lifecycle passed |
| Assert rejected-shot fallback origins | Adopted; existing rejection checks strengthened |
| Invalidate a previously selected cached muzzle through real setorigin | Adopted; existing relocation proof extended |

Per-frame callback checks alone are not evidence of uninterrupted native
input-to-animation scheduling; the new
sequence passes with native first-shot and scheduled-shot hand origins, 0.1s
cadence, release/repress, ammo exhaustion and scoped restoration. Physical-headset appearance remains user qualification.
