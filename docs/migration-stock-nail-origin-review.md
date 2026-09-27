# Pinned stock nail origin: design and senior review

Branch `2.0`. This is a narrow QuakeC boundary adapter for the installed
stock id1 `progs.dat` (340014 bytes; SHA-256
`f9a2d64e84a6530281c016f1a5557924370fdd9a1a10eb6e785d491352ffacf0`).
It does not replace the stock firing callback or change desktop/mod behavior.

## Behavioral reference and incompatibility

The exact bytecode's `launch_spike` is function 209; statement 4169 calls
`setorigin(newmis, org)` after assigning `newmis.owner = self`. Ordinary nail
source is `self.origin + (0,0,16) + v_right*ox`, with `ox` alternately +4 or -4.
Super-spike source is `self.origin + (0,0,16)`. A super nailgun with one nail
falls back to the ordinary source. The inherited generic weapon compensation
temporarily moves `self.origin` to the clamped physical muzzle minus eight
forward units and 16 up, so these stock projectiles originate eight units
behind that muzzle.

The behavioral reference is the installed stock QuakeC for ammo, alternation,
velocity, damage and effects, combined with the tracked, world-clamped muzzle
for VR. The existing `PF_setorigin` remains the sole spawn/link owner; the
existing private weapon-pose scope supplies the post-clamp muzzle. No second
projectile implementation, QC fork or additional persistent state is needed.

## Astra senior review and disposition

An Astra Max review verified the packed bytecode and approved the minimal
`PF_setorigin` adapter with two corrections. The implemented helper accepts
only the exact id1 SHA/size/CRC and function/statement, live private pinned
player, matching projectile owner, selected nail weapon and an applied,
unrelocated weapon scope. It checks the actual authored residual against
`{0, +4*v_right, -4*v_right}` in the current QC firing basis. This admits the
one-nail super-nailgun fallback while rejecting unrelated source expressions.

| Review choice | Disposition |
| --- | --- |
| Narrow pinned `setorigin` adapter and existing clamped muzzle cache | Adopted. The generic offset and other QuakeC callbacks remain unchanged. |
| Validate only finite, bounded lateral displacement | Tightened to the three verified stock source forms. |
| Add lateral offset after center clamp without another sweep | Rejected. A world sweep from center to barrel clips the final position at a nearby wall; centered spikes check the center for solidity. |
| Recompute generic offsets, fork QC or replace projectile spawning | Rejected as broader behavior and state duplication. |

The helper writes a local translated vector. `PF_setorigin` then uses its
normal relocation hooks, `origin` assignment and link; QC argument globals are
unchanged. If any pin or validation fails, `PF_setorigin` follows its original
path. Scope invalidation on player relocation and first-match nested lookup
remain shared with the prior stock shotgun adapter.

The final Astra diff review found that the cached center can itself be solid
when the earlier clamp falls back to the eye. The adapter now returns to the
authored QuakeC path on `startsolid` or `allsolid`, including for centered
super spikes. This avoids replacing a potentially clear stock spawn with an
embedded one; the exact wall/corner outcome remains a runtime acceptance case.

## Verification and limits

The Linux debug Ninja build passes. Static source and packed-bytecode checks
support the gate and source forms. The sandbox denies GDB ptrace, so this is
not an observed gameplay result. Live acceptance still needs both alternating
barrels, true super spikes, one-nail fallback, unobstructed and corner shots,
impact/damage/ammo/effects, scope restoration, and unchanged desktop and
nonstock QuakeC behavior. User headset testing is outside this implementation
goal, but the boundary should also be exercised locally when a runtime probe
becomes available.
