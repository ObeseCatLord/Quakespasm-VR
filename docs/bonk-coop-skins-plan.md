# Per-player Bonkjam co-op hammer skins

## Goal and verified reference

Allow independent hammer skins for each co-op player, in desktop and VR.
Preserve original models, sounds, hammer gameplay, selection pickups and
single-player behavior. Only the exact already-admitted Bonk program is in
scope. No mod PAK, QuakeC source or compiled program will be replaced.

Verified by reading the installed original `pak1.pak` authored QC and compiled function/global tables:

- `w_hammer.qc:15` declares global float `hammer_skin`; compiled offset 873.
- `item_keys_runes.qc:379-458`, function 376 `hammerskin_touch` (statement
  8697), assigns that global from the pickup's style and runs existing weapon
  reset/inspection for `other`, the selecting player.
- `weapons.qc:249-343`, function 412 `W_ResetWeaponState` (10722), selects
  `self.weaponmodel` from that global. Hammer sounds also read it.
- `client.qc:70,111,131` already carries skins in the existing per-client
  spawn parameter 11 for changelevel/respawn/decode. `SetNewParms` (627,
  16928) does NOT initialize parm11: late joins can inherit prior scratch
  parameters unless the adapter deliberately supplies a new player's default.
- `hammerskin_think` (377, 8899) compares global choice with a pedestal's
  style and disables its touch when equal; a shared disabled pedestal cannot
  represent independently selected skins. It preserves CFL_LOCKED.
- `SV_BonkHammerProgramLoaded` in `Quake/sv_phys.c` pins the complete original
  SHA/size/functions; the VR selected-model guard reads offset 873 directly.
- `PR_EnterFunction` / `PR_LeaveFunction` in `Quake/pr_exec.c` already own QC
  function frames, including nested calls, locals and reentrant native calls.
- Client initialization in `sv_main.c` zeroes reused slots and retains loaded
  spawn parms; Host save/load writes/restores per-client spawn parms. These
  are reusable persistence boundaries, not grounds for a new save format.

## Accepted minimal adapter

Reuse `client_t.spawn_parms[10]` as the single per-player skin owner for this
exact mod; this is the storage the authored mod already intended for skins.
At QC function entry/exit, give original global 873 a bounded actor scope.
Flush an active actor before changing scope; nested same-actor calls must
retain current writes rather than reload stale cache. Player `self` owns
ordinary weapon/sound/spawn functions; exact hammerskin_touch selects `other`.
Restore prior owner/value at return, including reentrant calls, VM switches
and error unwind. Outside QC, VR authorization reads the same player owner.
No per-opcode interception, general mutator system, duplicated weapon logic
or new protocol field is proposed.

At world hammerskin_think, use a temporary neutral selector so all unlocked
pedestals remain available to every player. Keep original lock and think
logic, and make touching an already-selected skin a per-player no-op so a
player standing on that pedestal does not repeatedly reset/inspect the weapon. New root SetNewParms must supply default skin 1 without overwriting
another player's selection through stale self/scratch values. Existing map
and respawn parm11 paths retain that player's current selection.

Alternative: add a saved extra entity float using existing engine extra-field
registration and wrap original QC calls. It is reusable but requires another
storage/persistence path and respawn snapshot handling; use it only if the
existing parm11 owner proves insufficient. Recompiling/replacing Bonk QC
would invalidate existing image/ABI admission and require distributing changed
mod content; rejected unless the adapter is demonstrably incorrect. A broad
per-opcode virtual global implementation is rejected for complexity and cost.

## Environment and review contract

| Fact | Evidence/status |
| --- | --- |
| Checkout / branch | vkQuake-based `2.0` checkout; main untouched |
| Installed assets | External Quake installation; read-only for tests |
| Original VM | SHA b54e33e50ad06d5628132a26bb6b089534d091bd2b6756799a15b61e7af49811, 684974 bytes; existing guard |
| Program function frame definitions | `Quake/progs.h`, prstack_t and qcvm_t |
| Original gameplay qualification | tests/Bonk_runtime.py, Bonk_runtime.gdb, Bonk_verify.py |
| Local debug build | build symlink outside checkout; no production deployment from it |
| Persistence through adapter | Existing parm11 owner retained; native qualification recorded below |
| Pickup visual ownership | One networked pedestal cannot show per-player selection alpha; make unlocked skins available to all |

Astra xhigh must verify load-bearing facts and challenge necessity of the
adapter, owner transitions and use of spawn parms. Return prioritized findings,
smallest viable implementation and exact remaining pitfalls (under 1000 words),
read-only; do not audit unrelated renderer, netcode, release infrastructure or
all melee families. No further human preference is needed for the specified
per-player behavior. Main integrates decisions; Sol codes the bounded slice.

## End verification plan

After implementation, use original QC with two players: distinct selections,
reselect hammer, repeated alternate selections, default pedestal availability,
locked pedestal rejection, player-specific sounds, VR model authorization,
new join/reused slot, respawn, changelevel and save/load. Test single-player and
another mod as controls. Prefer a native local server/client proof with private
configs/assets and software rendering; no physical headset/GPU or live Foundry
server changes. Commit on 2.0 and use the automated production release workflow.

## Astra xhigh review dispositions

The local review verified original QC and compiled ownership paths before
critiquing the plan. Main spot-checked the dead-player early return, root
connection setup, pedestal touch/think and the Host_Error unwind boundary.

| Recommendation | Disposition |
| --- | --- |
| Use existing client parm11 as sole persisted owner | Adopt; no extra field, cache or save format. |
| Seed change-parm scratch for dead/reset early returns | Adopt; root new-player defaults must ignore stale self. |
| Resume latest actor value across nested frames | Adopt; retain VM-local scope identity and current writes. |
| Neutral world think is insufficient alone | Adopt per-player same-style no-op and immediate native availability refresh; preserve locks and original eligibility. |
| QC errors bypass ordinary return hooks | Adopt cleanup at PR_RunError and Host_Error, with no stale-owner dereference. |
| Existing old saves can encode shared/stale selection | No broad legacy-save migration; qualify newly produced independent saves and existing valid parm11. Original shared selection cannot encode distinct historical choices. |
| Replacement QC / extra fields / opcode virtualization | Reject; existing function boundaries and parm11 suffice. |

The core adapter is expected to stay around 200-250 lines across existing VM,
server and error boundaries. A second persistence owner or general dispatcher
would reopen the design instead of becoming temporary scaffolding.

## Final Astra xhigh review

An independent local Astra xhigh review of the completed six-file patch found
no implementation blockers. It checked actor/generation ownership, nested
flush/restore, new/dead spawn parameters, original touch/think reentrancy,
locks, VM suspension/error/destruction and cached VR admission. No parallel
persistence or replacement gameplay implementation was needed.

The review requested two consecutive dispatches through a pedestal's actual
`touch` callback without a fixture think between them. The native fixture now
covers this, as does the two-network-client proof. Generation invalidation and
error/suspension cleanup received source review; no executed failure-injection
claim is made for those paths.

## End qualification

- The completed Linux Debug build passes. No production renderer or protocol
  was replaced; admission is cached once per program load, and function-scope
  work runs only for the admitted co-op program.
- Counterfactual: two actual network clients using the previous executable
  selected S-Blade and Katana, then both received Katana on weapon reset.
- Fixed executable: the same two clients receive and render
  `progs/v_hammer_sblade.mdl` and `progs/v_hammer_katana.mdl`, respectively.
  Both players also successfully dispatch the same S-Blade pedestal callback
  consecutively, without an intervening availability think.
- Private software Vulkan/Xvfb clients and an original-asset local dedicated
  server supplied this proof. There were no physical headset, audio/input,
  GPU-reset or live-server operations.
- Bonk single-player control retains the original global selector and disables
  its already-selected pedestal. Base Quake does not admit the adapter.

The final native fixture passes 143 observable checks using original QC,
native client connect/spawn, original weapon pickups, interleaved selection,
reselection, unlocked/default/locked pedestals, dead/new spawn parms, reused
client slots, start-map respawn, player-specific sound samples, VR own/foreign
model guards and actual native multiplayer save/load. Private socket records in
that fixture are not network admission evidence; the separate connected-client
proof above covers real admission and delivery.

Unexecuted coverage: a different-map network changelevel, non-start timed death,
dead-player save/load, generation/abort failure injection and physical headset
presentation/audio. Parameter capture/decode, living save/load and the relevant
source paths are verified; no wider claim is made. The adapter does not
reconstruct independent historical choices from old saves that stored a shared
selector.

The committed update uses the existing automated release coordinator for clean
Linux, native ARM64 and Windows production builds, Straight deployment and R2 /
GitHub publication. Immutable per-revision receipts identify actual source,
package verification and publication results; the Debug test executable is not
deployed. The launcher, mod content and live Foundry server remain untouched.
