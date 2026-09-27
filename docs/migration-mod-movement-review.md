# Mod-compatible predictive VR movement boundary

Date: 2026-09-27. Branch: `2.0`. Astra senior design review ran with effective
`gpt-6-astra` / `ultra` settings. It reviewed the selected private movement
owner against the local QSS-M and inherited OpenVR implementations. The
selected trial remains default-off and restricted to the pinned stock QuakeC.

The objective remains predictive VR co-op on modded QuakeC, including
AD-family mods, without replacing vkQuake's renderer, native desktop physics,
or the selected command queue. The current selected owner runs PreThink,
scheduled weapon Think, PMove, impacts and triggers, physical contact, and
PostThink for each completed command (`Quake/sv_phys.c`,
`SV_Physics_ClientPrivateWalkTrial`). It then publishes the completed command
cursor. This command lifecycle is worth reusing.

| Finding / option | Disposition |
| --- | --- |
| Removing the stock progs identity check would immediately enable mods. | **Rejected.** The selected handoff removes presumed stock QuakeC water/jump velocity changes before PMove. Its dry jump branch can restore the entire earlier velocity, erasing a mod force. WALK hull and absent `customphysics` do not prove that a mod follows stock movement semantics. |
| Add a qualified mod movement adapter at the existing QC-to-PMove boundary. | **Adopted as the next design direction.** Qualify one exact installed progs identity and assign jump impulse, drag, flags, teleport/waterjump timers, and mod-authored forces to explicit owners. Identity identifies the qualification; it is not behavioral proof. Keep the existing selected queue and completion tail. |
| Invoke `SV_RunClientCommand` or copy QSS-M's movement body. | **Deferred / rejected respectively.** The 2.0 QC hook is declared but not invoked and `PF_sv_pmove` is not integrated. QSS-M's dispatch is a useful reference, but copying its command owner would duplicate vkQuake callbacks and lose the VR queue contract. A cooperative QC hook needs a defined movement/callback/replay contract first. |
| Use vkQuake native movement for unknown mods before selection. | **Adopted.** It preserves the working desktop/public fallback. Do not switch a selected command to native after some callbacks have already executed. |

Astra also found a concrete inconsistency in the preceding moving-platform
slice: selected admission and physics allowed robust BSP pusher ground, but
`SV_PrivateWalkTrialStateValid` in `Quake/sv_user.c` still rejected it on the
next packet. The packet gate now matches the `sv_gameplayfix_elevators >= 3`
condition while retaining its ground-offset validation. This fixes a
source-proven disconnect path; a stock lift still needs end-to-end proof.

The first mod software proof should pin one installed AD-family binary and
confirm its movement-code correspondence, then compare native and selected
dry WALK, jump, and a real mod-authored force. Record origin, velocity,
flags, timers, callback effects, and completed ACK. Compare client replay
separately with the existing shadow trace. Expand to water, teleport, and
moving supports only after that contract holds. For the lift regression,
send a **new** selected command after pusher ground has been committed; a
prequeued command alone will miss the old packet-gate defect. The user will
perform device and performance measurements later.

## Installed q30a1024 movement evidence

The installed `q30a1024/progs.dat` under the Straight game tree is 2,347,206
bytes, SHA-256
`5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`,
Quake CRC16 `0x2ad3`, and vkQuake's folded-MD4 `0x5960bef3` (computed with
the actual `Quake/mdfour.c` implementation). Its function table labels
`PlayerPreThink`, `PlayerPostThink`, `PlayerJump`, `WaterMove`, and
`CheckWaterJump` in `client.qc`; it has no `SV_RunClientCommand` function.
The binary global-definition table exposes `map_jumpheight` as a saved float
at global slot 582; its field-definition table exposes `onladder` and the
jump-boots state. A future adapter should resolve and validate these names
against the pinned identity, rather than hard-code the slot in generic PMove.
The installed `decomp` archive contains a decompiled `client.qc` and a
`progs.src` whose header identifies an FTEQCC 5753 build. That archive and
the separate `my_progs/client.qc` are **not byte-identical**; do not treat
either as a verified exact source build of this binary merely because names
match. The decompiled movement structure is useful behavioral evidence, but
binary/source correspondence still needs qualification.

The decompiled `PlayerPreThink` calls water movement and jump logic, plus
grapple/ladder handling. Its `PlayerJump` uses a map `map_jumpheight`, supports
jump boots with air-jump and forward-velocity changes, and can replace
velocity for a ladder jump. The mod's settings may override `map_jumpheight`
per map. `PMSV_BuildMoveVars` currently publishes a constant vanilla
`jumpspeed`; the selected stock correction also restores all pre-PreThink
velocity on a grounded jump. Both would be wrong for at least some of these
mod states. The first AD adapter must prove how PMove receives the dynamic
jump value and when QuakeC, rather than PMove, owns a boots/ladder/grapple
impulse. Keeping server replay off alone does not fix authoritative double
movement.

## q30a1024 handoff follow-up review

A bounded review of the installed q30 binary found two `map_jumpheight`
additions, a boots-height Z replacement, a whole-velocity replacement, and
`onladder` clears. These are opcode-level observations, **not** proof of all
branch predicates or exact correspondence with the available source-like
files. The existing selected owner restores the whole pre-PreThink velocity
for a grounded jump and then gives PMove the original jump button and
pre-PreThink release state. A q30 force can therefore be erased or followed
by another jump. Merely using the post-PreThink velocity delta or the final
`onladder` field cannot reliably identify which QC branch ran.

An Astra senior review (effective `gpt-6-astra` / `max`) challenged the
proposed subtract-and-readd jump adapter. Its key additional finding was
that the selected path can execute zero-time maintenance callbacks and up to
eight completed command lifecycles per world frame. The q30 contract must
cover those callback frequencies, not only one PreThink per visible frame.
It also found that ACK authority and client prediction permission are
separate; a server-correct ordinary jump is not yet predictive boots or
grapple support.

| Recommendation | Disposition |
| --- | --- |
| Qualify exact q30 binary by SHA-256 and resolve named QC global/field definitions | **Adopt.** Identity pins the target, while validated runtime values remain necessary. Do not cache a stale VM offset across progs reload. |
| Build a new dispatch/movement owner | **Reject.** Reuse the selected command queue, callbacks, contacts, stat export and ACK tail. |
| Infer ordinary jump from post-QC velocity delta and subtract/readd it | **Reject.** Boots, ladder and grapple can replace or add velocity in overlapping callbacks; final state is insufficient branch evidence. |
| Let QC own a proven jump impulse and suppress PMove's corresponding jump | **Investigate as the smaller adapter.** It must also preserve takeoff, ground classification, release state and timers: PMove can reground a small upward velocity even with jump suppressed. This is not yet general q30 admission. |
| Use dynamic `map_jumpheight` as a movevar | **Adapt.** Validate the live value and its update timing before export; the binary's initial value is zero, so a zero-valued startup snapshot is not a usable jump speed. |
| Fall back to native physics after an unsupported live command starts | **Reject for this slice.** Phase-aware terminal continuation is not qualified for living q30 commands, and callbacks must not run twice. Keep unknown mods on native movement before selection. |
| Enable client prediction after server parity | **Defer until branch state is available to replay.** Existing `MOVEACK_FLAG_PREDICTION_ALLOWED` checks stock conditions, not boots/ladder/grapple state. Server-authoritative correction is an intermediate proof only. |

The next software proof should use the exact installed binary with a remote
pinned client, compare native and selected ordinary takeoff and a ground boots
jump with forward input, then repeat across a zero-time maintenance interval
and two commands in one world frame. Record visible takeoff, apex, horizontal
travel, landing, boots state and completed ACK with declared tolerances. Ladder,
grapple, water and general q30 admission remain separate gates. No headset or
performance measurement is required for this proof. Until the executed QC
branch and PMove takeoff ownership are qualified, keep q30 excluded from the
selected path.
