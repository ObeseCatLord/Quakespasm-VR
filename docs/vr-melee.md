# Adding VR weapon collision and melee support

This is an engineering guide for adding a mod or weapon revision. For player
and server settings, see [Server settings](server.md#physical-weapon-contact-development).
Support must preserve the installed mod's QuakeC, desktop attacks, tracked VR
poses, and existing movement/networking. A weapon-wheel entry or a calibrated
`vr_weapons.txt` does **not** establish immersive melee compatibility.

## What is automatic, and what is not

General weapon obstruction is client-side: `VR_ResolveWeaponGeometry` in
`Quake/vr.c` traces the body-to-grip and weapon segments through
`CL_TraceWeapon` in `Quake/pmove.c`. It retracts the rendered weapon without
moving the actual controller or player. Both eyes use the same geometry.
Retraction also adjusts the ordinary VR firing muzzle sent with movement input;
it is not purely cosmetic. Physical melee contact endpoints and speed remain
raw tracked/calibrated values, independent of that display/muzzle correction.
It requires the server's advertised collision capability and the client option.

This is not triangle-by-triangle mesh collision. Ordinary weapons use
grip-to-muzzle geometry; supported melee profiles select verified cutting-edge
vertices from the model. Those vertices receive the same scale, handedness and
calibration transforms as the rendered weapon. Fists and split weapons have
their own verified geometry. A broad gun stock, side-mounted blade, or unusual
replacement can therefore need better contact geometry than the generic segment.

Automatic mesh-derived collision proxies are possible, but are not implemented
as a universal fallback. A whole-viewmodel bounding box includes decorative
arms, hands and effects; treating it as the damaging surface gives false hits.
If extending geometry generation, first prove a minimal load-time proxy using
the existing tracing path. Separate the obstructing volume from the damaging
surface, allow explicit overrides, and retain the generic fallback when a mesh
cannot be classified. Do not add per-frame triangle networking or replace the
movement solver merely to obtain a more detailed shape.

Geometry cannot determine gameplay semantics: damage, ownership, readiness,
charge tiers, ammo costs, corpse gibbing, healing, lunges and projectiles come
from the mod. These require a verified, narrow engine adapter. Unknown game-code
or model revisions must retain native trigger attacks, not silently acquire
stock-axe behavior or lose their fire button.

## Networking and authority

| Responsibility | Owner |
| --- | --- |
| Track controllers, transform geometry, resolve local rendering and ordinary firing muzzle | Client |
| Submit grip/base/tip samples and physical speed with movement commands | Client |
| Validate weapon identity, reach, continuity, cooldown and world obstruction | Server |
| Resolve damage, native button touches and parries | Server |
| Send confirmation haptics and normal gameplay results | Server |

`MOVEEXT_VR_CONTACT` in `Quake/protocol.h` is negotiated and accompanies the
existing relative-VR movement input. `Quake/cl_input.c` makes samples relative
to the command's player origin; the server processes accepted commands in
`SV_VRContactProcessCommand` in `Quake/sv_phys.c`. It sweeps between samples,
not merely at the latest rendered position. No mesh or client-selected damage
result is transmitted. Disabled/unsupported peers retain the old input path.

Local obstruction/muzzle resolution does not wait for a round trip. Gameplay contact and its
confirmation feedback do. Parrying uses current validated server-side pose
history, without historical lag compensation. Client geometry/speed reports
are bounded and checked, not a proof of trustworthy physical tracking.

Do not move damage or QuakeC execution to the client to reduce perceived latency:
clients can lack entities outside their visibility set, disagree about moving
targets, or send fabricated hits. A future predicted cosmetic response should
reuse the existing sweep and be reconciled with server confirmation, without
duplicate damage, sound or haptics. It is not currently a second authoritative
contact system.

## 1. Audit the effective mod, not its folder name

Record the actual loaded `progs.dat` and model identities, including search-path
and PAK precedence. Verify that any source checkout matches that bytecode.
Compare the deployed server, installed client and distributed package too:
successful connections do not imply identical game-code layouts. QBJ3's
distributed and locally optimized programs require separate verified adapter
descriptors even though their native wrench and berserk interfaces agree.
Follow each weapon from pickup/ownership and selection through input dispatch,
ready states, attack stages, damage helpers and delayed callbacks. Audit all
weapon banks, alternate modes and power-ups, not just familiar inventory bits.

For each weapon record:

- Model(s), effective ownership/selection fields and the exact ready condition.
- Native trigger press, hold and release behavior, ammo thresholds and recovery.
- Damaging surface, grip, scale, animation and attached support-hand geometry.
- Hit/miss/brush/corpse behavior, effects, movement impulses and special attacks.
- Supported VM/model revisions and the evidence that pins each revision.

Classify it before editing:

| Class | Immersive behavior |
| --- | --- |
| Ordinary axe, wrench, sword or fist | Physical strike; suppress canned trigger swing and hold verified ready pose |
| Charged melee | Physical effort selects the original charge stages, not new damage formulas |
| Continuous chainsaw | Keep native trigger cadence/release; allow obstruction and button poking |
| True hybrid | Keep native trigger stages; add one physical contact attack when ready |
| Explicit projectile/stab split | Trigger projectile, physical stab, with specifically verified native branches |

Gungnir is the last case: its trigger fires shards without the canned stab/combo.
Do not generalize that exception to all hybrids. Scimitar, Mace, Rapier and
supported hammers retain their native trigger stages. Bonk keeps native charge
tiers and movement effects; Honey keeps its native downed-zombie gib behavior.

## 2. Extend the existing adapter at its narrowest boundary

Start with `Quake/vr_melee_qc.h` and the relevant `vr_melee_*.h` family. Reuse an
existing family only after verifying its ABI and behavior. The adapter supplies
an accepted contact to the mod's original immediate damage/effect path; it does
not replace QuakeC or introduce another combo, inventory or cooldown scheduler.

Pin VM identity, function names/indices, entry statements, parameter/local
layouts, relevant opcodes/call sites and required constants. Parameter widths
alone are insufficient: a one-word argument can be a string, float or entity.
For example, Drake's hammer damage helper takes a damage-type **string**, not
an attacker entity. Test lethal outcomes, not just a nonlethal health decrement.

Use the existing scoped hooks in `sv_phys.c`, `pr_cmds.c` and `pr_exec.c` only
where necessary. Scope interception to the verified root/function/call site and
call depth. Preserve native validation, button guards, cooldowns and effects.
Borrow only the required synchronous aim/trace context, then restore it; do not
roll back a whole player edict and erase legitimate inventory or movement changes.

Specific hazards to check:

- Native attack preludes can set reloads, lunges or touch callbacks before damage.
  Skipping the prelude is not equivalent to calling the damage helper.
- Primary and secondary traces have different meanings. A physical sweep's
  temporal fraction is not a native ray's distance fraction.
- A death/touch callback can change QC `self`, free the target or teleport it.
  A now-invalid accepted hit must not become an unrestricted fallback ray.
- Radius attacks must retain native target qualification. Restrict iteration
  without corrupting global entity `.chain` links or dereferencing an invalid cursor.
- Projectile placement uses the accepted command's calibrated muzzle. If a
  verified synchronous projectile helper needs a borrowed origin, restore it
  before body effects and delayed callbacks; reuse existing muzzle safeguards.
- Delayed native thinks must not create extra animation-driven melee after a
  physical hit. Do preserve legitimate projectile flight and native recovery.
- Test repeat-target fields through naturally occurring hits. Seeding a field
  that the first hit is supposed to establish can conceal a broken branch.

## 3. Add client geometry without breaking rendering

Inspect the profile table and identity/readiness helpers in `Quake/vr.c`.
For MDL assets, update the exact source tracking allowlist in
`Mod_TracksImmersiveMeleeMDL` (`Quake/gl_model.c`) when required: a profile whose
source identity was never retained cannot match. Pin source identity, pose
format, frame/vertex dimensions, ready frame and edge vertices.

Choose the complete damaging blade/head, not two adjacent vertices that happen
to lie at its tip. Verify bounds visually on model-only renders and numerically
after held-model calibration. Do not use weapon-wheel display scale. Test
left-handed mirroring and both controllers for paired weapons.

Record the retained hand's anatomical handedness, not just which side of the
source mesh it occupied. QBJ3's split wrench retains a left hand: its renderer
and raw contact edge use the same handedness XOR and scale-independent raw
grip centroid. Controller-locked grips must share one transform between render
and contact; do not independently rewrite the user's original model offsets.

Keep supported identity distinct from readiness: an animation passing through
frame zero is not necessarily ready for a physical swing. Native-trigger
hybrids must keep their animations. Use canonical alias animation state via
`R_SyncAliasViewmodelAnimation` in `Quake/r_alias.c`, including active MD3/MD5
selection and classic fallback, rather than a temporary drawing copy.

When a model contains two arms attached to one weapon, inspect existing exact
source generation recipes in `Quake/vr_mdl_split.h`. Verify retained triangles,
winding, skins, animation and offsets against the source. Remove only the
unwanted support-hand geometry. Split paired weapons must remain aligned with
their respective controllers. Generated mod-local cache files are acceptable;
do not bundle third-party mod assets as a shortcut.

## 4. Preserve reset, input and co-op policy

Reuse the existing contact history and movement-discontinuity lifecycle.
Weapon changes, death, reconnects, mod changes, lost tracking and explicit view
rebases must not connect two unrelated samples into a damaging long sweep.
Physical speed must exclude locomotion, snap turns and visual retraction.
The ordinary stroke gate is 0.1 m/s and 1 cm of physical motion; rearm requires
a received sample below 0.05 m/s. Keep parry stroke classification in sync with
this gate, while preserving the separate Bonk charge tiers and native cooldowns.
Keep physical melee base/tip positions raw and calibrated too: do not feed
collision-retracted display or ordinary firing-muzzle positions back into the
physical sweep. Otherwise wall retraction itself can become a false stroke.

Combat reach assistance (`sv_melee_hitassist`) is a separate, current-time
query after the physical sweep misses. It reuses world/guard validation and
only accepts actors, not shootable switches. Never put its synthetic ray in
tracking history or use it to measure swing effort. Keep within-ray spatial
ordering (near guard versus farther body) distinct from temporal ordering
between sampled sweeps. An assisted hit has time 1; callbacks cannot reopen an
earlier part of the stroke. Preserve the native player hull: comfort reach is
not a reason to change movement, floor support, or which gaps a player fits.

Do not invent another packet stream or bypass accepted-command processing.
Check legacy input and PMove, held/latched buttons, queued commands, stale input
and sequence gaps. Unsupported or disabled melee must restore native controls.

Parrying follows existing player-collision policy: competitive/classic co-op can
parry; the normal no-player-collision co-op profile cannot. Explicit individual
co-op settings override the preset. Do not change friendly-fire or telefrag
policy as a side effect of adding a weapon.

## 5. Verification before enabling a revision

Use isolated test game roots with read-only asset links. Do not link writable
user configs, saves or `vr_weapons.txt` into a fixture. Establish a native
desktop/disabled oracle using the same effective mod bytecode, then exercise the
actual physical-contact consumer, not only a mocked helper.

At minimum cover:

- Hit, miss, wall, moving brush, occlusion, lethal hit and supported corpse hits.
- Ammo boundaries, power-ups, charge tiers, native trigger stages, held/released
  input, repeat attacks, cooldown and idle-state transitions.
- Native sparks/sounds/healing, movement impulses and actual projectile
  think/touch behavior after the attack returns.
- Targets that die, disappear or teleport during callbacks; nested QC state.
- No attack through a wall; no attack from locomotion or a tracking discontinuity.
- Parry, recovery/rearm, both hands, left-handed mode and scaled offsets.
- Classic/enhanced selection, unsupported replacement fallback and animation
  settling, including mod/weapon changes.
- Real connected-client input, command batching/loss/staleness and resets;
  unchanged desktop and feature-disabled behavior.

The maintainer workspace has a private `tools/immersive_melee/runtime_matrix.sh`
and `tools/regression_tests/run.sh`. They and their fixtures are intentionally
local-only, not available in a public checkout or GitHub Actions. Extend and run
them when available; otherwise create equivalent isolated local reproductions.
Never commit proprietary mod data or these private test tools.

Obtain an independent design/correctness review for new semantic hooks. Include
source/bytecode evidence, the native oracle, actual contact results and known
limitations. Challenge whether the adapter can be smaller before adding hooks.
Build/test success alone is not headset gameplay validation; report the actual
verification performed and leave untested comfort/feel explicitly unverified.

When qualifying a second game-code revision, keep independent fixtures for both
programs. Verify actual client capability negotiation as well as native contact
outcomes, draw/idle readiness and button callbacks; testing only the local
program can miss a server silently retaining native trigger attacks.

For a controlled test, enable `sv_weapon_collision 1` and
`sv_immersive_melee 1` on the server, with the corresponding client VR options
enabled. Paired support also follows `sv_akimbo`. Keep development server defaults
unchanged unless changing that policy is explicitly part of the task.
