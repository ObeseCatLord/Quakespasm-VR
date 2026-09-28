# AD admission: current-owner Astra design brief

Historical pre-review brief. Astra's completed `gpt-6-astra` / `max` design
review and main's disposition are recorded in the
[admission plan](predictive-mod-admission-2.0-plan.md#astra-disposition-before-production-changes).
The provisional alternatives below preserve what was submitted, not the final
chosen architecture or a claim of completed mod qualification.

Resolve the next implementation contract, not a stock-only migration finish.
The complete [mod admission plan](predictive-mod-admission-2.0-plan.md) is
committed as `3ce7a77d`; stock activation `fd26537a` is implemented. Solo operator,
reuse adapters, no parallel physics/protocol. Main has rechecked current source
and decoded the installed binary. Read-only review <=900 words; verify first,
rank architectural leverage, recommend an executable first slice that advances
ordinary mod prediction and preserves the complete parent scope. No tests,
edits, subagents or renderer/device research.

## Verified environment and changed evidence

| Fact | Evidence / implication |
| --- | --- |
| Workspace and constraints | `quakespasm-2.0` on `2.0`; user-dirty `docs/migration-2.0.md` is excluded. Linux local software checks only; user defers live/device/eye, Windows/ARM and performance. No sockets/ptrace escalation; do not build a test transport. |
| Current admission | [verified source] `sv_main.c:855–929` enables stock selection by default, exact profile/program/valid state/stats/elevator guards. q30 has the dormant jump handoff but is excluded from begin; its replay is unconditionally disabled (`2014`). |
| Crucial prior conclusion is stale | [verified source] `sv_phys.c:9113–9243`, existing `SV_Physics_ClientSelectedNativeFrame` now stages actual queued levels/latches/roomscale, sets `private_move_native_frame=true`, calls **SV_ClientThink**, and reuses native frame QC/collision/contact/completion. `sv_user.c:495–629` permits native Gorilla/input acceleration when that flag is set. The earlier review's missing native input acceleration does not apply to this fresh-frame path now. Mid-callback native continuation is still not a general living fallback. |
| Stock selected lifecycle | [verified source] Selected WALK stages each eligible command, PreThink/PMove/contacts/PostThink per command; maintenance uses zero time. A shared stack Think window grants one world-duration scheduled-Think opportunity. Receipt and snapshot classification must agree, with strict WALK after callbacks. Native modes/death/stock freeze already reuse native frame/remaining phases. |
| Authority is already distinct from permission | [verified source] `sv_main.c:1956–2055`: selected engine or native frame authority; existing mode epoch updates; replay granted only for matching state, authority, timers/support. New authority bytes or another lifetime are not justified. |
| Exact q30 binary reverified | [verified read/decode] Installed `quakespasm_straight/q30a1024/progs.dat` size2347206, SHA256 `5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`, matches existing helper. Function1143 PreThink PCs54204–54431; 1144 PostThink54432–54830; 1145 Jump54831–55074; 1146 WaterMove55075–55258; 1147 WaterJump55259–55306; 1108 GrappleHook_Client51809–52156. No `SV_RunClientCommand` function or `customphysics` field. |
| Cadence hazard remains actual | [verified bytecode/current source] PreThink calls GrappleHook_Client at54328 and contains ladder velocity*0.9 at54381–54384 plus Z clear54385–54386. Repeated selected callbacks/maintenance differ from one native world callback, even when frametime is zero. PostThink calls W_WeaponFrame at54645. The existing scheduled-Think window does not cover these. |
| q30 state surface | [verified binary definitions] Float globals `intermission_running`490, `secloc_running`733, `cinematic_running`694, `map_jumpheight`582. Player floats `onladder`417, `timeladder`420, `moditems`320, `oldgravity`231, `pausetime`173; entity fields `entladder`419, `hookent`718. These are evidence, not offsets to hard-code. Existing source-like `/tmp/q30-source-check/client.qc` and `client_ghook.qc` aid reading, but are not an exact-source proof; use binary for disputed branch facts. |
| Existing narrow proof and reuse | [verified source/recorded executed evidence] `tests/q30_movement_native_fixture.c` compares real q30 QC/native/selected dry jumps, staged boots/ladder, batching/maintenance. Injected selection is not ordinary admission. `qc_jump_owner` retains authored impulse/release and low-takeoff support; PMove's normal jump adds jumpspeed along gravity. Reuse solver, native collision, customphysics, input ABI definitions, registered custom stats and queue/completion. |
| QSS-M cooperation | [verified pinned source] `../QSS-M/Quake/sv_user.c:635–704` supplies input globals, PreThink, either mod command hook or PF_sv_pmove, PostThink. Unaware-mod path restores all pre-QC velocity, unsuitable for authored boots/grapple. 2.0 declares input globals/hook but has no server hook invocation/PF_sv_pmove. Existing `pr_cmds.c:PR_GetSetInputs` already supplies QSS-style ordinary input ABI for the client input filter; reuse it instead of another bridge. Existing `SV_CollectPMovePhysents`, `PMSV_BuildMoveVars` and shared PMove/collision are reusable server boundaries. Do not copy receipt-time dispatch. |

## Provisional state/phase table and options

| State | Current usable owner / missing contract |
| --- | --- |
| Stock WALK | Qualified per-command shared PMove/QC; preserve it. |
| q30 ordinary dry WALK | Native world currently works; dormant selected QC-jump handoff exists. To admit predictive commands, resolve whether unaware Pre/PostThink must stay once per world, then match ordinary jump/velocity/release replay (live map height versus generic270). |
| q30 boots/ladder/grapple/wet/holds/cameras | Native fresh frame now has input + once-world QC/physics + ordered contact drain. Before any callback, this is a reusable candidate for states without a client contract. But identifying a state at frame start is not proof against a callback entering another state mid-command. Preserve complete session transitions; no disconnect-on-ability policy. |
| Selected native modes/death/freeze | Existing coalesced input/completion and phase adapters remain reference. q30 has camera/secret/cinematic states as well as intermission; stock-only freeze predicate cannot simply be applied to them. |
| Cooperative QC program | Reuse QSS-M input/builtin ABI through existing selected world dispatch; hook needs a real consumer and complete command/callback/replay contract. Installed q30 is not that consumer. |
| Quiet/loss/batch | Ordinary native retains held input/world callbacks; selected queues own explicit duration and maintenance. Define force/QC clocks and credit retirement; do not advance both native world physics and command PMove for the same elapsed movement. |

Current lean: **reuse existing fresh native frame for unpredicted mod states,
and extend existing command owner only where a matching solver/QC/replay
contract is established**. An intermediate all-native selected bit is not a
finished mod prediction feature; ordinary compatible gameplay must become
predictive and later states must stay functional.

Real fork A: preserve once-world unaware QC around the existing PMove command
batch for ordinary q30, with transient frame scope, native fresh dispatch chosen
before callbacks for incompatible abilities. This directly addresses cadence,
but changes callback placement/input aggregation and needs a proved mid-callback
policy, jump release/press handling and quiet-time force ownership. Reject a
new callback scheduler or persistent duplicate QC lifetime.

Real fork B: retain per-command QC and narrowly adapt every proven duplicate
force/cadence branch. Reuses present loop, but manipulating final velocity cannot
identify branches and could become a per-mod reimplementation. Main currently
disfavors this unless the necessary delta is demonstrably smaller.

Real fork C: implement cooperative QSS-M hook/builtin first, leaving unaware
q30 native until its contract is resolved. It advances a real missing engine
feature, but does not itself satisfy q30/Mjolnir modern movement. No imaginary
consumer or claim that this closes AD admission.

## Requested disposition

Challenge these forks against **current** native reuse and the full outcome.
Specify the smallest architectural contract and exact owners/stages that can
reach ordinary mod prediction without losing authored gameplay. Identify the
load-bearing decision to resolve first and whether a narrow vertical software
comparison would actually discriminate it. Keep existing native/QC/collision/
transport policy unless demonstrated incompatible. Reopen the plan if a world
QC scope or broader living phase adapter is necessary, before coding. Do not
recommend indefinitely accumulating dormant paths or declaring stock-only or
all-native compatibility the finished goal. State effective model/effort and
source-evidenced blockers versus unverified behavior; terminal <=900 words.
