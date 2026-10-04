# Tracked weapon shot-roll fix

## Evidence

`master` commit `336046f9` added a QBJ3-only post-`PF_makevectors` adapter.
It retained controller roll only for QBJ3 Pistol/Flak, while the existing 2.0
private weapon-pose scope already owns temporary player angles, globals,
calibrated source reconstruction, and restoration.

The stock source calls `makevectors(self.v_angle)` in `W_Attack`, then calls it
again in `FireBullets`. AD has the same `W_Attack` call and its player branches
in both `FireBullets` and projectile-pellet `Launch_Shells` call it again.
Their non-player branches use `self.angles`; those must remain ordinary world
math. This makes a one-use latch incorrect: one admitted player scope can
legitimately need the same tracked lateral basis multiple times.

## Implemented narrow adapter

On an admitted private pose, save the finite raw hand angles before clearing
`ent->v.v_angle` roll. QuakeC continues to see roll zero. A single scope-angle
selector supplies the raw basis to both the initial global `AngleVectors` call
and `VR_WeaponCalibrationProjectileSourceOffset`, so QC that consumes the
already initialized globals has the same 6DOF lateral axes as its calibrated
source reconstruction.

`PF_makevectors` still runs ordinary `AngleVectors` first. Its fallback then
replaces only `v_right` and `v_up` when the active QuakeC `self` is the live,
unrelocated scoped player; input, temporary `v_angle`, and saved tracked
pitch/yaw exactly match; and every vector is finite. Matching calls are not
consumed, so stock/AD `W_Attack` and firing-leaf calls remain rolled until the
scope restores or invalidates. Forward stays QuakeC's pitch/yaw result.

Other selves, world/entity angles, nested or relocated scopes, desktop play,
and altered player angles retain native axes. Pair-admitted scopes do not save
the generic shot basis: Dwell and Enyo still take their existing specialized
`PF_makevectors` paths, while QBJ3/Enyo source adapters retain ownership of
their selected hand and source origin. Snack calibration/source rules remain
in the existing calibration helper and receive the same scoped raw angles as
every other admitted non-paired ranged pose.

Senior review narrowed generic raw-basis admission to the outermost same-player
scope with no inherited relocation/invalidation. Nested source reconstruction
retains its existing behavior; the new roll adapter cannot admit that borrowed
temporary origin as a fresh generic shot.

## Focused fixture

`tests/vr_shot_roll_scope_fixture.c` includes the production `sv_phys.c`
adapter with a minimal QC VM. It invokes the actual private-pose begin/end
functions and verifies body/hand source reconstruction, a 90-degree rolled
calibration input, zero camera roll, repeated matching shot bases, nested-scope
masking and cleanup, restored origin/angles/globals, and relocation invalidation.
Tracing and calibration are typed fixture seams; calibration database parsing
is covered separately. It rejects a different `self` and player world angles.
The ASAN/UBSAN fixture passes. It does not launch a game or automate VR.

## Exclusions

No protocol, input, camera-angle, generic mod-name, or new weapon policy is
added. Manual in-game verification remains the only way to confirm every
third-party QuakeC firing layout.
