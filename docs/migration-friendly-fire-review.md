# Shared co-op friendly-fire adapter: Astra senior review

The inherited `sv_nofriendlyfire` behavior is a server callback shield, not
VR weapon logic. The donor temporarily sets QuakeC `teamplay` to zero and
other active players' `takedamage` to `DAMAGE_NO` for an attributed player's
Think, impact or PostThink callback. The player can still hurt themself.
Attribution uses the callback entity's player slot or its immediate owner
slot, including a now-inactive owner of a delayed projectile. Nested callbacks
inherit the outer shield. This does not promise immunity to direct QuakeC
health writes or to every mod-specific damage path.

| Design choice | Disposition |
| --- | --- |
| Adapt the donor's single shield at the existing server callback boundaries for desktop and VR. | Adopt. Native QuakeC remains the sole damage implementation. |
| Cover stock physical callbacks that may damage; for Dwell, scope the pinned `W_FireAxe` leaf on hits and whiffs. | Adopt. The donor starts Dwell shielding at the leaf, after its non-damaging prelude. Do not include unrelated button-touch callbacks. |
| Restore valid temporary state before shutdown/disconnect QuakeC; discard the snapshot before VM destruction or reload. | Adopt. A frame-end reset alone is too late. |
| Track a different attacker for nested callbacks. | Reject for this compatibility port; donor behavior keeps outer attribution. |
| Intercept damage at a generic QuakeC boundary instead. | Defer: stock and Dwell damage ABI/effect order have not been proven equivalent. |

The cvar defaults to `0`, has notify/serverinfo flags, and activates on any
nonzero value while `coop` is active. It does not inherit `sv_coop_classic` or
its tri-state profile rules. One outer snapshot owns restoration; a nested
entry must not acquire or clear that ownership. Check server VM and entity
lifetime before restoring, and preserve an intentional QuakeC change to a
different damage mode where it can be distinguished from our temporary
`DAMAGE_NO`. Exact restoration cannot be claimed when QuakeC independently
writes the identical value during the callback.

Implementation boundaries in the donor are `Quake/sv_phys.c`'s
`SV_FriendlyFireBegin/End`, scheduled Think, both impact directions and
PostThink paths. The target should reuse those existing dispatch sites and
add explicit scope around damage-capable physical callbacks. The donor Dwell
adapter begins its shield immediately before `W_FireAxe`; its prelude runs
outside that shield. `Host_Error` and
`Host_EndGame` can run `ClientDisconnect` during teardown, so restore before
those callbacks and invalidate before clearing/reloading server QuakeC.

Consolidated software proof should compare two-client health, armour,
velocity, pain/death and hit effects under native and physical attacks, whiffs,
projectile and delayed hits, self-splash and monster/world damage. Check the
option off, coop off, classic mode, fractional/negative nonzero values,
nested callbacks, disconnect/reuse and both abort paths. Dwell needs its
own comparison once its physical outcome is connected. This design review
does not certify parity.
