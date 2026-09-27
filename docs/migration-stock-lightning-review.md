# Pinned stock lightning on the vkQuake/OpenXR branch

Branch `2.0`. This adapter covers the installed stock id1 `progs.dat` only
(340014 bytes; SHA-256
`f9a2d64e84a6530281c016f1a5557924370fdd9a1a10eb6e785d491352ffacf0`;
loader CRC16 3064; the separate `progs.dat` header CRC is 5927). The installed
PAK bytecode was parsed using the structures in
`Quake/pr_comp.h`; the local stock QuakeC source agrees with these sites.

## Reference and incompatibility

Stock `W_FireLightning` (function 205) traces from `self.origin + 16 up` at
statement 3983, writes `TE_LIGHTNING2` with that source at statements
3995/3998/4001, then calls `LightningDamage` (function 204). Its center and
two side damage traces are statements 3860/3891/3914. The generic VR pose
places temporary `self.origin` at the clamped physical muzzle minus eight
forward and 16 up, so the beam starts behind the muzzle and damage starts
behind and below it. The current vkQuake client also overwrites a local beam's
network start with the desktop player origin; inherited OpenVR attached it to
the VR hand.

The stock `LightningDamage` bytecode discards the return value of
`normalize(f)` and makes both horizontal components of its side offset equal
before multiplying by 16. Those side offsets are **not** generally 16 units.
The adapter preserves their actual QuakeC values rather than correcting that
unrelated stock behavior.

## Astra senior review and disposition

| Recommendation | Disposition |
| --- | --- |
| Change temporary `self.origin` only while a lightning weapon is selected | Rejected. Weapon impulses can switch during the enclosing Think/PostThink scope, and broad origin changes can affect other callbacks. |
| Borrow exact pinned `traceline` and `WriteCoord` builtin sites | Adopted. The original QC owns ammo, wet discharge, sounds, beam packet policy, target uniqueness, particles and damage. |
| Preserve authored side offsets and endpoints | Adopted. The damage adapter translates each start by the physical muzzle displacement after validating the corresponding authored endpoint; it does not normalize or cap the stock side offset. |
| Clear a blocked physical muzzle or side start | Adapted. A solid physical beam muzzle rejects the whole adapter; once the beam is admitted, a solid translated damage start returns a handled, non-damaging world trace rather than firing from the old QC origin. |
| Preserve local VR beam attachment without mutating network state | Adopted. The client uses a frame-local beam start from the current accepted private muzzle; the packet start remains available when tracking is invalid. Desktop body attachment is unchanged. |

The server's private weapon-pose scope stores only the clamped muzzle, the
borrowed beam endpoint and a short-lived first-damage endpoint. This last
value keeps later side checks stable if a target's pain callback changes the
global `v_forward` after the center trace. Relocation invalidates the borrowed
state. The exact program hash/CRC, QuakeC function/statement, live private
player, current weapon and authored source checks prevent other programs,
desktop clients and Shambler lightning from taking this path.

The final Astra diff review found a near-wall hybrid path: falling back to a
stock trace after a translated damage start was blocked could deal damage from
behind the already-translated beam. The adapter now handles that lane as a
world miss and lets QuakeC continue its normal uniqueness/remaining-lane logic.

## Verification boundary

The Linux debug build verifies the code links. Packed-bytecode and source
checks establish the site pins. Headset and eye tests are the user's later
work; local runtime observation of lightning is still needed to prove local
and remote beam position, physical damage, both side lanes, final-cell shots,
wet discharge, weapon switches, wall/corner starts, tracking loss, roomscale,
desktop play and mod beams. No comparative performance measurement is a 2.0
completion gate; diagnostics remain available for the user's later measurement.
