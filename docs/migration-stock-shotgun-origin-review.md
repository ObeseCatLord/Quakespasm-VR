# Stock id1 VR shotgun pellet origin

The installed Straight `id1/pak0.pak` contains the pinned 340014-byte
`progs.dat` (SHA-256 `f9a2d64e84a6530281c016f1a5557924370fdd9a1a10eb6e785d491352ffacf0`).
Its `FireBullets` function is index 192 and calls `traceline` at statement 3646.
The bytecode at 3622–3628 starts with `self.origin + v_forward * 10`, then
overwrites `src.z` with `self.absmin_z + self.size_z * 0.7`. The latter does not
follow the temporary VR hand-origin translation, because normal weapon-scope
entry does not relink the player. This is the installed program's behavior,
not an assumption about a similar QuakeC source tree.

The adapter retains the existing `PF_traceline` and `SV_Move` owners. One
private weapon-use scope stores the clamped physical muzzle. For the exact
id1 program, player, weapon, `FireBullets` call site and expected QuakeC source,
the builtin traces from that muzzle to `muzzle + (QC_end - QC_start)`. QuakeC
continues to choose direction/spread, pellet count, damage and ammo. Other
programs, public/desktop players, monsters and unrelated traces use the
existing path. The scope caches its hash-backed program admission after the
first pellet, retaining the muzzle for every pellet in that attack.

| Astra senior-review finding | Disposition |
| --- | --- |
| A nested same-player scope can reconstruct a muzzle from its parent's temporary origin. | **Adopted.** Such an entry does not cache a shotgun muzzle; the innermost scope masks its parent. Relocation clears cached validity in all matching scopes. |
| The stock axe identity helper also recognizes other programs. | **Adopted.** Reuse its SHA-256-backed admission and require id1's descriptor CRC 3064. Check the active private client, edict, QuakeC `self`, weapon and exact function/statement. |
| A Z-only patch misses the forward discrepancy. | **Adopted.** Translate all three ray coordinates while preserving its original displacement. |
| `PF_aim` can auto-select a target from the inherited temporary origin. | **Deferred.** This adapter corrects ray origin only; enabled-autoaim target geometry needs separate qualification. |
| Relinking the player, rewriting QuakeC or intercepting interpreter statements adds broader behavior changes. | **Rejected.** Keep the native trace boundary and existing collision owner. |

The review ran as Astra `gpt-6-astra` at `xhigh` (reviewer reported runtime
metadata). The Linux debug executable builds with the adapter. The installed
program and current source support the mismatch and adapter scope, but a real
pellet and damage outcome is not yet proven. A local GDB probe was blocked by
this sandbox's `ptrace` restriction. The remaining
implementation check is a headless accepted-command run with unobstructed and
wall-clamped muzzles, controlled target damage, SG/SSG/one-shell fallback
counts, scope restoration/invalidation, and unchanged desktop/public/mod
behavior. User headset testing remains separate.
