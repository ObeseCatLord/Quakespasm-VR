# QBJ3 akimbo server adapter: senior design review

Branch `2.0` already transports two optional akimbo poses in its pinned private
move command and has one scoped server-side VR weapon pose. The split-model
loader and strict client capability receiver are present. The missing vertical
is server-side per-hand firing by QBJ3's own QuakeC. Astra Max reviewed this
decision against the inherited QBJ3 implementation and the installed QC
source; the current server must advertise no akimbo capability until the
vertical is proven.

| Review finding | Disposition |
| --- | --- |
| Extend `sv_vr_weapon_pose_scope_t` for the two accepted hand poses rather than porting the donor's second context. | **Adopt.** The existing scope owns callback lifetime, saved origin and basis, nesting, and restoration. A second context would duplicate those policies without a demonstrated incompatibility. |
| Gate the hook by mod directory, QC function, viewmodel, weapon and animation frame alone. | **Reject.** Those names do not prove the source formula. Both local QBJ3 program revisions have the same header CRC. Pin the loaded program's SHA-256 and size. The installed candidate is 905470 bytes with SHA-256 `de2c6a60df24f5ce0c3fc41b0fd6309105a0ea7ae895dfb4a6867950b9b90e34`. |
| Restore the player origin and basis at `PF_aim` return. | **Reject.** QBJ3 stores `aim()` in a local vector and computes the projectile origin afterward, from player origin, view offset, forward/right/up and the frame-specific lateral offset. Keep the selected pose until the outer QC callback completes. |
| Invalidate only the innermost pose on `setorigin`. | **Reject.** Relocation must deactivate the hand pose in every matching nested scope, so a later `aim()` cannot reuse a pre-teleport body anchor. Retain the existing origin restoration/relink policy. |
| Use the most recent connection move receipt time for hand-pose freshness. | **Reject.** Queued/maintenance commands can outlive that receipt. Use the staged command's existing receipt timestamp so a new packet cannot refresh an old pose. |
| Port only the donor's `PF_aim` entry hook. | **Revise.** Also use the selected *physical muzzle* for the best-target autoaim correction. The temporarily compensated QC origin is not the ray's physical start. |
| Replace QBJ3 firing with a C-side weapon simulation or a new protocol. | **Reject.** Native QC must continue to own ammo, cadence, damage, effects and projectile spawning. |

The smallest proof loads one exactly identified QBJ3 `progs.dat`, sends pinned
private commands with separated left/right muzzles, and observes frame 11 and
15 native QC shots. Check projectile origin and velocity, ammo, cadence,
damage, player state after the callback, world-wall clamping, relocation,
nested scopes and old queued commands. Desktop/public clients and missing or
invalid poses must use the ordinary weapon path. Moving-platform think code
also consumes player origin before outer restoration; test that path and adjust
only if reachable during this weapon callback.

An independent original-index bytecode read of that installed candidate used
the donor's `tools/immersive_melee/qbj3_enyo_proof/extract.py` parser. In
`weaponanim_twinnailgun_loop`, statements 16833-16849 select `offs = +4`
for frame 11 and `offs = -4` otherwise after the 11/15 firing gate, then call
`W_FireTwinNailgun`. In that function, statements 16895-16914 call
`makevectors(self.v_angle)`, call `aim`, store its vector, compute
`origin + view_ofs + 11*forward + offs*right - 6*up`, scale the direction
by 1996, and call `launch_projectile`. These instructions match the relevant
installed source formula. This is a bytecode/source contract for one exact
program, not an end-to-end gameplay result or a claim about other revisions.

The first implementation may add the narrowly gated hook with advertisement
disabled. Capability negotiation and client paired rendering/input follow only
after the QC and end-to-end shot proof. Enyo, berserk and Dwell are separate
later adapters, not reasons to broaden this one.

An Astra xhigh code review of the first server-hook commit found no verified
source-level blocker. It did find that a rejected nested scope could leave an
older pose visible to `PF_aim`; the scope now pushes a masking frame even when
admission fails and pops it without restoring entity state. The review also
noted that zero-duration maintenance commands would fail the akimbo freshness
gate, but that maintenance owner currently admits only a stock program, not
QBJ3. That path remains deferred until QBJ3 can enter it. Native QC shot
behavior still requires the focused runtime test before advertisement.
