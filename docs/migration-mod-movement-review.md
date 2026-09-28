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

## Native customphysics adapter

The inherited branch and QSS-M both dispatch the `customphysics` entity field;
the vkQuake-based branch declared that field but previously never called it.
The native client dispatcher now uses that QSS-M boundary after PreThink and
velocity validation. A populated callback replaces scheduled Think, engine
movement and Gorilla movement for that frame, then uses the existing native
link/PostThink/completion tail. A removing callback skips that tail. Other
entities use the same callback before native movetype dispatch; their callback
owns linking and Think, as in QSS-M.

`SV_RunCustomPhysics` retains the entity across QC and uses the existing
friendly-fire callback scope. It intentionally runs at the body origin, since
the temporary VR weapon muzzle pose is inappropriate for a callback that can
move the body. `SV_GorillaEligible` excludes an active customphysics callback
so ordinary input acceleration is not deferred to hand locomotion. The selected
PMove owner continues to reject customphysics; this adapter does not expand
prediction permission or trial admission.

The Linux SDL3 build and ASan/UBSan interpreter fixture pass. The fixture uses
real QC bytecode for body relocation and callback removal, while allocation
and field lookup remain controlled boundaries. The fuller native dispatcher
GDB probe is included but unexecuted: this sandbox denies `ptrace`. No real-mod
callback trajectory or connected-peer prediction is certified by this slice.

## World-frame Think ownership review

Astra `gpt-6-astra` / `max` reviewed the proposed stable native command owner
against the actual dispatch, QSS-M source and installed q30 bytecode. The main
agent independently verified the q30 multiply constant and the selected
batch/Think/TOSS paths before applying this disposition:

| Recommendation | Disposition |
| --- | --- |
| Add a continuously living native queued owner | **Rejected for this slice.** Reusing collision does not establish mod callback cadence compatibility and, without replay, adds no immediate prediction capability. It would also require distinguishing queue ownership from solver selection across existing input, Gorilla and snapshot gates. |
| Keep exact q30 on native frames until a bounded behavioral comparison passes | **Adopted.** q30 statements 54381–54386 multiply velocity by the stored 0.9 constant and clear Z in a ladder branch, without using frametime. This verifies a callback-cadence hazard, not that every repeated callback takes that branch. The existing QC jump handoff remains reusable; stock-only admission stays closed. |
| Give the selected owner one world Think opportunity | **Adopted and implemented.** A client-local stack window spans the command batch and maintenance/terminal continuations. The first scheduling position consumes it even if no Think is due. Later commands preserve nextthink. TOSS respects the consumed opportunity. Scheduled Think receives the original world duration; the existing QC world time and clamped scheduled time remain unchanged. Scoped durations are restored afterward. |
| Preserve native clocks, movement and completion/retirement owners | **Adopted.** Native clients pass no window and retain their existing scheduling. Selected movement and Pre/PostThink retain command time; maintenance retains zero-time input from the last completed command. Completion stays after the lifecycle and retirement stays in SV_FinishPrivateUsercmds. No new authority/protocol or queue was added. |
| Integrate a cooperative SV_RunClientCommand hook | **Deferred.** The installed q30 program has none; adding an unneeded hook owner would not solve its unaware-QC timing contract. |

Before this fix, each selected command reopened the same world Think window.
A callback rescheduling within it could run multiple times in a batch of up
to eight commands. The Linux offline native fixture now loads real stock
assets/QC and executes the production selected dispatcher with diagnostic QC
callbacks. It covers two/eight commands, a not-due opportunity with later
PostThink scheduling, maintenance with no queue/insufficient credit, and
death in Think or a second PreThink followed by TOSS. Completion, retirement,
clock restoration, pending Think and callback counts are checked. The fuller
customphysics native dispatcher cases pass in that same fixture, superseding
the interpreter-only evidence above; the GDB version remains unrun.
An isolated negative-control build that reopens the consumed Think window
fails the two-command callback-count assertion; the unchanged production
build passes both native fixture markers. This demonstrates that the fixture
detects the scheduling defect, rather than merely exercising the new helper.

Exact-q30 native/selected trajectories, boots/ladder/grapple behavior and
connected private/public snapshot/replay proof remain separate gates. This
change neither qualifies q30 nor broadens mod prediction permission. Device
testing, Windows/ARM checks and performance measurements remain deferred by
the user.

## Native friction boundary uncovered by mod comparison

The exact-q30 ladder comparison exposed a separate native vkQuake input bug:
with `sv_friction 0`, the analytical ground-friction crossing calculation
used `log(1)`. At stopspeed it produced `0/0`; above stopspeed it also led to
non-finite arithmetic. Velocity validation then erased that momentum.
`SV_UserFriction` now enters the logarithmic decay path only for `0 < r < 1`,
using its existing classic formula for non-decaying friction as well as
one-tick stopping values. Positive ordinary friction retains its existing
analytic behavior. The offline native fixture directly checks unchanged,
finite 99/100/101-unit momentum with zero friction; the earlier customphysics
and selected Think cases still pass after the shared bootstrap extraction.

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

## Exact q30 jump branches and PMove takeoff boundary

A follow-up read-only decode of the installed SHA-pinned `progs.dat` qualified
these bytecode branches in `PlayerJump` (function 1145, statements 54831–55074):

| Branch | Verified predicate and write |
| --- | --- |
| Ground boots | `moditems & IT_ARTJUMPBOOTS`, `FL_ONGROUND`, and `FL_JUMPRELEASED` at 54875–54884; adds `map_jumpheight` to vertical velocity at 54913–54915. |
| Ordinary ground | Without the boots branch and outside the ladder branch, requires `FL_ONGROUND` and `FL_JUMPRELEASED` at 55044–55050; adds `map_jumpheight` at 55071–55073. |
| Boots extras | Air boots set vertical velocity to `jumpboots_height` at 54957–54959; positive `jumpboots_forward` and lower horizontal speed gate the forward velocity write at 55015. |
| Ladder jump | `PlayerPreThink` calls `PlayerJump` for `onladder == LADDER_VEL` with `button2`; statements 55017–55042 replace velocity with `v_forward * map_jumpheight`. The boots branch has priority if both are set. |

The bytecode predicates support letting QuakeC own q30 jump selection and
velocity, instead of classifying an impulse from the final velocity delta.
They do not qualify grapple, water, callback side effects, or client
prediction. Exact source-file correspondence remains unproven.

A temporary, uncommitted probe reused the real donor floor hull and
`PM_PlayerMove` from `tests/pmove_migration_fixture.c`. With the jump button
suppressed and a QC-authored initial upward velocity, 120 units/s was
re-grounded to zero at the floor for both untimed and 100 ms commands; 300
units/s moved upward and stayed airborne. The threshold comes from
`PM_CategorizePosition`, not q30 QuakeC. Consequently, simply masking the
PMove jump button is insufficient when a map sets a low `map_jumpheight`.
The adapter needs an explicit takeoff handoff that preserves water/ladder
classification and permits later landing, while leaving desktop/stock PMove
unchanged. This probe is a solver observation, not native-vs-selected mod
parity or proof that q30 should be admitted yet.

## q30 custom-stat compatibility

The same installed SHA-pinned binary registers nine custom stats from
`worldspawn`: float `moditems` at 40; string `ckeyname1`–`ckeyname4` at 50–53;
and float `ckeyskin1`–`ckeyskin4` at 55–58 (statement PCs 47982–48014).
Those slots do not intersect the private movement slots 225, 226–229,
238–239, or 241–255. Selected admission now checks the actual registered
slot ranges instead of rejecting every mod custom stat. This removes an
unrelated admission barrier; the pinned stock-progs check still excludes q30.

The stat sender and client parser already support string updates under
`PEXT2_REPLACEMENTDELTAS`, but `PR_CustomStat` previously rejected
`ev_string` registration. The whitelist now permits it, so q30's key-name
stats can use the existing transport. Vector registration also rejects a
starting slot too close to the array end. The sandbox blocks UDP sockets,
so a dedicated-server/client round trip for these stats remains unverified.

## Living q30 command-owner decision

Astra `gpt-6-astra`/`max` reviewed the proposed handoff to vkQuake's native
movement dispatcher *before* an unsupported q30 command begins. The main
thread spot-checked the load-bearing paths in `sv_user.c`, `sv_phys.c`,
`sv_main.c`, and `cl_main.c`. This is a design finding, not a q30 admission or
gameplay result.

| Recommendation | Disposition |
| --- | --- |
| Reuse the selected command queue, completion cursor, retirement, and snapshot transport. | **Adopt.** They already encode completed-command ownership and should not be copied into a new protocol. |
| Invoke the existing native frame dispatcher for a living selected q30 command that is unsupported by PMove. | **Reject for now.** Selected clients skip `SV_ClientThink`, so the native frame would lack its ordinary input acceleration. Terminal continuation also coalesces commands and uses world-frame time; neither path proves correct living-command actions, Think timing, or rollback. |
| Rely on `private_move_native_frame` to make a live native fallback safe. | **Reject.** Packet receipt and snapshot admission still require a selected WALK owner and may disconnect after an ability changes state. An older snapshot can also have already authorized client replay before the fallback ACK arrives. |
| Keep q30 under its existing native owner while qualifying a dry QuakeC jump handoff. | **Adopt as the next implementation direction.** The adapter must preserve QC velocity **and** `FL_JUMPRELEASED`, bypass stock velocity restoration, and keep prediction permission off until matching client replay is proven. |

The smallest software proof is an exact-binary q30 dry static-floor sequence:
low positive jump, forward movement, held jump through landing, release, then
another jump. Compare native and selected origin, apex, horizontal travel,
landing, effects, callback counts and timing, completed ACK, paired commands,
an intervening zero-time maintenance pass, and a due player Think. Passing
that proof qualifies only dry jump; boots, grapple, ladder, water, changing
movetype/custom physics, and general q30 admission remain separate work.

The selected owner identifies only the exact installed q30 image using the
loader-cached SHA-256. Its QuakeC callback supplies the live `map_jumpheight`
impulse. PMove keeps vkQuake's generic movevar builder: client replay is off
for q30, and its jump check is bypassed, so exporting `map_jumpheight` as a
PMove value had no dry-jump consumer. That extra builder also made a map jump
above `sv_maxvelocity` or a zero startup global invalidate an unrelated
command or snapshot, unlike native post-QuakeC velocity clamping. The
stock-only admission gate remains unchanged.

The exact-profile handoff lets q30 QuakeC retain its jump impulse and
post-PreThink release latch if the selected owner is ever entered. It bypasses
stock whole-velocity restoration and hands the authored velocity to
`PM_PlayerMove` with `qc_jump_owner` throughout press **and release** commands.
The floor probe must not re-ground a low, still-rising takeoff just because the
next command releases the button. Stock selected movement still uses PMove's
jump. Selected q30 snapshots withhold client replay permission. This remains
inactive while q30 is excluded from admission; boots, ladder, grapple, wet
movement, native trajectory and callback parity are not qualified.

## q30 handoff senior review after implementation

Astra `gpt-6-astra`/`max` verified the narrow handoff and found a real
cross-command takeoff defect plus an unnecessary movevar failure condition.
The main thread checked the relevant floor probe, release flag, velocity clamp,
and movevar consumers. The floor-hull fixture now runs a 5 ms low takeoff
followed by button release, and the Linux SDL3 build passes.

| Recommendation | Disposition |
| --- | --- |
| Preserve QC jump ownership across button release. | **Adopt.** The exact-profile owner now sets `qc_jump_owner` for every q30 selected command and reads the post-QuakeC release latch. The fixture exercises two consecutive commands on the real donor floor hull. |
| Separate `map_jumpheight` validity from native velocity limiting. | **Adapt by deletion.** The selected dry path has no PMove jump consumer or client replay for q30, so the q30-specific movevar builder was removed. QuakeC still reads its live global, and native velocity clamping retains its existing timing. |
| Keep q30 on native movement while wet/ability transitions remain unqualified. | **Adopt.** Stock-only admission remains. Dryness at selection cannot guarantee a permanently dry session. |
| Compare trajectory and callback timing, not merely initial impulse. | **Adopt as the next proof gate.** Native analytic gravity, PMove substeps, maintenance passes and weapon Think timing may differ. |
| Replace the selected command owner or add a living native fallback. | **Reject.** Neither is required by the narrow handoff, and the prior review found unsafe state/timing boundaries. |

The fixture proves only the shared solver's floor behavior; no q30 QuakeC or
client-server session was executed. The next exact-binary comparison must cover
low immediate-release takeoff, held/rejump, forward motion, paired commands,
maintenance and due Think, with origin/velocity/flags, effects, callbacks and
completed ACK observed together. Passing that still would not admit ordinary
q30 play until wet and ability transitions have an owner.
