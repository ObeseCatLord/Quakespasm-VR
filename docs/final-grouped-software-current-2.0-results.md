# Final grouped software verification — current progress

2026-10-02. Executed only after all confirmed D03/D04 production repairs were
reviewed and committed at0f277d1f. The [reviewed remaining list](remaining-issues-consolidated-2.0.md)
is unchanged. These are bounded results, not whole F01–F08/F10 acceptance.

The current assertion-enabled debugoptimized Linux graph refresh compiles the
changed native sv_user object and relinks successfully. Main compares every
recorded graph source input: only sv_user.c changed since the complete225-object
build, exactly the reviewed repair. Current dependency graph/official SteamAudio
SDK remains enabled. All source files are read directly from2.0; no borrowed old
renderer graph for these grouped CPU runs.

## Network, physics and save group

Twenty-two native loaded-engine cases compile/link/run0: six metadata sender/
parser/signon/refusal cases, nine local loopback movement/save/load/fastload/
pending-owner cases, four native-versus-selected lifts with desktop/generated VR
input, two real BSP liquid-transition cases, and one customphysics/Think/native
handoff case. Stock QC/BSP, native allocator, command and snapshot owners run.
Transport/signon/prepared input and excluded graphical seams retain the existing
fixture limits. No connected public peer or headset claim follows.

Two old fixtures required test-only signature repair: native respawn policy uses
its existing owned-default NULL argument; snapshot writers share a real
SVFTE_BeginFrame; liquid disposable preview uses the existing upmove mode.
Initial compile failures remain in the private logs. No assertions or expected
thresholds were weakened; final current binaries pass unchanged predicates.

Six independent strict-warning/UBSan native components compile/run0: loopback
sequence/reconnect, datagram/NAT/IPv6 owner routing, initialized XR input adapter,
stereo camera/native command-angle integration, weapon schema and shared solo/MP
weapon/projectile calibration. Scripted transport/input sinks and camera GPU-wait
spy remain explicit. They do not certify connected reliability, rendered UI,
physical controllers or actual GPU fences.

Receipts: FastGames/qsvr-final-native-groups-te91bniv, compile/link argv/logs,
profiles/*/run.log, qualification-status.json, input-transport-status.json and
main-verified-summary.json. Failed initial compile logs retained. Main verifies
all22case outcomes and all6component outcomes; no graphical runtime was started.

## Native audio group

The existing complete controlled audio suite is rebuilt against the refreshed
current graph. Compile/link, original CPU music-signal generation and combined
run each exit0; all13distinct native pass markers appear once. Opus/relay/reset,
VAD/budgets/recovery, stored route/permission policy, fatal-send/client teardown,
spatial native/HRTF, callback/room worker/music/device-retry owners run together.
SDL dummy capture exclusively; transport remains captured and peer state
prepared. This refresh does not close remaining live delivery, active-XR route
and host/device callback integration gaps or human listening/device tests.

Receipts: FastGames/qsvr-final-audio-f0n61m8q, build/signals/run argv/log/status,
markers.json and main-verified-summary.json. No real microphone recording,
GPU/runtime/service/driver/system changes.

## Native request regressions

All12targeted D03/D04 cases pass through actual SV_RunClients and
SV_ReadClientMessage, loaded SSQC and native dropclient/spawnclient. Freed
numbered entity references reach present handlers or absent-handler refusal
without reviving the slot; out-of-range requests use world fallback and following
commands retain alignment. Unterminated capability/QC/native prefixes have no
effects. Self-drop and immediate replacement retire old messages exactly once
without delivering trailing opcodes to the replacement. Ordinary QC/native
terminated controls remain admitted.

The assertion-enabled graph compiles/links/runs successfully. A supplemental
-O2/-DNDEBUG sv_user object passes the same12cases with assertion-enabled fixture
and Debug support objects; it is explicitly not a full Release engine run.
Offline message/socket seams remain bounded. Main checks exact markers and source
hashes in FastGames/server-request-final.9ftBEN/main-verified-summary.json.

## Connected graphics qualification

Two controlled native NVIDIA attempts fail Vulkan device creation; NVML responds
but kernel channel allocation reports NV_ERR_RESET_REQUIRED. Both owned isolated
null runtimes are cleanly stopped. No GPU reset, driver reload, system graphics
change or unrelated process termination is attempted. GPU-dependent acceptance
remains open.

A separately signed official Mesa CPU driver, private Xvfb and isolated simulated
Monado null runtime provide diagnostic connected execution without NVIDIA. The
default profile admits movement, shots, replay and ACKs but observes no stable
ACK/sent pairs between sends, so the original prediction oracle fails without a
rendering opportunity. No production prediction bug or pass is inferred.
The explicit320x240/8Hz diagnostic observes54successful desktop replay returns
and8.702units between-send displayed displacement with unchanged ACK/sent/owner/
authoritative origin, followed by convergence. It still fails the separate short
jump timer assertion: native jump_secs expires above50ms and these commands are
longer. Thresholds and failure remain unchanged; this is bounded prediction
evidence, not a complete suite pass. The unchanged public baseline separately
passes desktop gameplay with266.981units movement, four shells consumed and
advancing ACKs. XR cannot start: the owned Monado null compositor reports
VK_ERROR_INITIALIZATION_FAILED during its own vkCreateDevice. No XR lifecycle,
clean public peer quit or D02 resolution is claimed from this aborted aggregate.
The experimental lower-rate runner is retained privately and removed from the
repository rather than introducing a qualification mode incompatible with the
short timer oracle. Both owned CPU services exit0.

Receipts: FastGames/qsvr-final-cpu-vulkan-bdm_i5vc and
qsvr-final-gpu-resumed-khdhlxgs. Signed private driver selection is confined to
child environments; no system installation or global runtime setting.

## Protected-output preparation

The patterned emitter and current native parser-fixture client compile/link0,
using the225-object refreshed graph, original enabled dependencies and renderer.
Mode0/1 behavior is preserved; test-only mode2 uses native opaque alias state.
The checker selects aligned coarse-fragment sensitivity from OFF captures before
comparing FIXED/E, requires both eyes and consumed default opaque alias pipelines,
actual generated/uploaded map equality and a stable outside-world difference.
Missing sensitivity is inconclusive. Main reviews the test source and verifies
asset generation, native strict compile/link and Python syntax.

Layer selection is source-derived from eye/map-layer count and the native
multiview/image-view setup; it is not independently observed Vulkan layer routing.
No protected GPU output pass is claimed. Receipts:
FastGames/qsvr-foveation-final-prep-jec4o61u/{status.json,sources.json,assets,
compile-argv.json,link-argv.json,compile.log,link.log}.

## Refreshed Linux package startup

Current shipping Linux engine0f277d1f executes directly through its recorded
native system interpreter and relative RUNPATH, with no global library override.
A disposable licensed id1 profile, explicit IPv4 loopback and disabled IPv6,
-dedicated2/-nosound/-nosteamapi loads e1m1, prints native status and quits0.
Native loader/dependency/startup/map/normal terminal behavior is established;
no graphical desktop, XR, mic or connected gameplay follows. Engine SHA256
c002c0f1b669c206ccaa6ed4c09fc83e7fca36c30944e84719badec9561d63cc.
The earlier0afcb00a hash refers to the installed pre-staging engine, before
patchelf applies the package RUNPATH; staged and installed hashes are distinct.
Nonfatal Steam-library/localization/MD5-skin warnings remain in the log.

Earlier probe failures are retained: directly invoking a bundled Ubuntu loader
with external host libc is outside the documented native-interpreter contract
and crashes before engine startup; disabling all UDP on the ordinary dedicated
entry naturally refuses unavailable networking. No packaging/engine fix is
inferred. A first corrected native startup also initializes default IPv6; final
repeat explicitly uses -noudp6. All children exit; no runtime/service deployment.
Receipts: FastGames/qsvr-linux-package-final-run-6gem4__j,
ipv4-only-argv.json/ipv4-only-status.json/ipv4-only-run.log plus the earlier
argv/status/debug logs.

## Still running or awaiting final execution

The current production0f277 shipping cohort is reconciled;
protected-output test inputs/capture/checker compile and await GPU execution. Live-metadata refusal and autosave qualification reuse existing native fixtures. Remaining V rows,
D01/D02 connected oracle/terminal evidence and final local Astra integration
signoff stay open until their actual outcomes are recorded. These results must
not silently substitute component passes for those completion boundaries.

## Additional native metadata and autosave acceptance

V03 opt-in producer admission now runs actual local signon, native cvar/Cmd
front doors, full reliable-buffer refusal and explicit successful retry through
the server parser. Five refused mutations preserve cvar strings/defaults/values/
flags, client/server/peer metadata and every queued byte; admitted retries reach
native peer stores with complete ordered commands. Six historic profiles also
pass in both the original build recipe and the optional enhanced build. The
extra wrappers are gated by METADATA_LIVE_ADMISSION_FIXTURE so original links
remain valid. Main integrated build_default/build_live_admission/default/live
all exit0 at FastGames/qsvr-metadata-v03-integrated-ykidgucc.

The autocvar wrapper observes no calls in this stock profile, but these cvars
have no loaded AUTOCVAR binding. This does not establish actual bound-VM
preservation or close all V03. Initial concurrent old-recipe wrapper-link
failure is retained and corrected by the optional compilation guard.

V06 opt-in autosave uses actual initialized co-op loopback, loaded stock QC,
native v7 writer and ordinary load. It rotates two slots, defers too-early
progress without consuming it, refuses a temporary-file open failure while
preserving both previous saves/baselines/slot, retries after native backoff, then
loads the written armor value and continues native movement. Main verifies
COOP_AUTOSAVE_NATIVE_PASSED and LOCAL_LOAD_NATIVE_PASSED with exit0 in
FastGames/qsvr-coop-autosave-v06-qualification-_ej20fth. Owner times and secret
counters are fixture-controlled; failure is temporary-file open, not final
rename. Other save dialect/identity/hub acceptance remains with V06.

## Platform cohort reconciliation

Windows native Release/Debug compile/link/package, Linux x86-64 package and
native Foundry AArch64 build/package complete from production0f277d1f. Main
independently hashes all653Linux and650ARM inventory records, and both Windows
16-artifact inventories. All match. Test/docs commits through6e06b824 change
no shipping input. Receipts: FastGames/qsvr-platform-final-2mx9pur9,
source-comparison.json, scope-results.json, main-inventory-verified.json and
platform-specific manifests. New production discovery repairs will require
affected engine/source reconciliation before final A01 closure.

## NET-020 startup and request repair

Local Astra/xhigh source review confirmed existing discovery/public-master/RCON
handlers lacked their startup cvar registrations and identified adjacent
RCON prefix admission and OOB challenge framing defects. Main accepted these
within V01/F01, inspected the handlers/readers, and reviewed Luna's two-file
implementation. Committed58fb8864 reuses existing SV_Init cvar objects/UDP
handlers, preserves public0/empty-password defaults, rejects unterminated or
2048-byte RCON fields before execution, and admits one bounded printable
challenge while preserving native terminal-NUL and newline query forms. No
parallel discovery service, protocol, heartbeat clock or state owner is added.

All405 recorded native graph inputs are compared; only net_dgrm.c/sv_main.c
change. Their objects compile and the225-object graph relinks0. Ten actual
headless native UDP cases pass: default public0/empty RCON password; explicit
controlled local-master heartbeat; getinfo/getstatus exact challenge/dialects;
seven normal optional-challenge termination forms; eight unsafe/overlong query
refusals; complete rule enumeration without password disclosure; wrong/valid
RCON controls; four incomplete/oversized RCON non-effects; actual native
hostname mutation. Both owned servers quit0. External masters are explicitly
cleared before publication; licensed assets remain read-only.

Receipts: FastGames/qsvr-native-discovery-requalified-4xs5e4m8/result.json and
profile logs/argv, plus qsvr-native-discovery-repaired-bq1zanaw/native-build-status.json.
Initial post-fix probe encountered delayed duplicate rule responses on its
shared observer socket. Separate request sockets preserve framing predicates
and establish the final result; the failed run remains retained. This does not
close all NET-020 connected gameplay/challenge/ProQuake/master-client obligations.
Current shipping refresh is underway for this production commit.

## Latest controlled Nvidia attempt

After the user reported recovery, the current58fb8864 native graph again fails
vkCreateDevice in the desktop peer. Contemporaneous kernel allocation logs
report NV_ERR_RESET_REQUIRED. The connected aggregate fails; no rendered XR or
protected-output success follows. All owned peers stop and isolated null runtime
exits0. No resets, driver reloads, system graphics changes or unrelated process
termination. Receipts: FastGames/qsvr-final-gpu-current-kyud4ace, connected
result/logs and runtime exit.json. Further GPU qualification stays paused.

## Connected RCON diagnosis and authored v6 load

The wire probe's first run waited for svc_serverinfo before answering the native
cmd-pext query; it timed out after actual reliable packet0/ACK. Main corrected
that fixture ordering by inspecting SV_SendServerinfo/SV_Pext_f, retaining all
protocol/ACK/movement predicates and the failed receipt. The next current run
reads actual serverinfo15 and completes five client reliable commands with
actual ACKs plus seven server reliable fragments. First RCON edict1 then makes
the native server exit1 with PR_SwitchQCVM: A qcvm was already active.

Main resolves the stack to RCON -> native console command -> borrowed SSQC
context in SV_RunClients. Read-only QSS-M already suspends/restores that VM and
restores the driver/ordinary shutdown guard. This is a demonstrated existing
V01/NET-020 defect, not a Nvidia failure or a new checklist feature. A bounded
[reference-reuse plan](rcon-qc-context-final-2.0-plan.md) precedes the repair and
local Astra review. Receipts: FastGames/qsvr-native-discovery-repaired-bq1zanaw/
native-udp-coexist-mwrsybya and qsvr-udp-coexist-requalified-r3t24gfd.

Independent V06 qualification loads an actual read-only inherited v6 id1 save
through the current ordinary dedicated engine, using copied save bytes/private
profile and linked licensed packs. Native status reports start/16maxplayers,
reserved player edict1 stays free pending restored identity, and normal quit0.
Source-save hash is unchanged. This establishes actual authored v6 load and
pending-slot admission only; saved living-player restore/movement remains
prepared for after the RCON repair. No generated save/header substitution, GPU
or physical inputs. Receipt: FastGames/qsvr-authored-v6-save-final-0bwynx03.

## User completion-scope update

2026-10-02: unavailable tasks/tests are outside the goal. The current GPU device
allocation/XR runtime and unavailable physical headset/gaze/provider checks are
excluded completion gates, with their outcomes retained as unverified/failed
observations rather than successes. Available CPU-native verification, platform
build/package freshness and final local Astra review continue. This scope
instruction overrides older acceptance wording; it does not remove implemented
OpenXR, optional gaze, stereo or foveation features.

## Current native connected RCON, loss and living v6 restore

Reviewed production079f4431 reuses the QSS-M borrowed-QCVM boundary and ordinary
Host_Error redirect cleanup, with both driver indices restored after status.
The current225-object native graph rebuilds/relinks0; engine SHA256
936635e2b4c9a82eca39e08b55c66e06baf93cf8b1b8dded05eafdca36b3fba8.
Docs-only scope commit5b6c491d changes no shipping input.

Three actual UDP gameplay runs pass on that engine. Each admits the native
NetQuake/ProQuake peer, answers cmd-pext before reading actual serverinfo15,
receives five reliable command ACKs0–4, spawns a walking player and sends20
unreliable movement packets. Native edict displacement exceeds93units while
getinfo/getstatus/rule/RCON-echo queries run; actual connected status and edict
commands succeed without nested-VM error. Public discovery truthfully advertises
network3/game15 and the connected player. External masters remain cleared.

The loss case deliberately omits server reliable ACK0, observes one actual
native retransmitted fragment, ACKs it, then completes signon and gameplay.
The authored v6 case restores the real saved living player's origin464/320/24
(native printed precision; saved z24.03125), health100 and shells25 before
movement. It uses a private byte-identical copy of the read-only save; reference
and copy hashes match afterward. Every owned server exits0 through ordinary
stdin quit with the peer pumping native ACKs during teardown.

Main independently verifies all three result receipts, exact engine hash,
command ACKs, loss duplicate and restored state. Root:
FastGames/qsvr-rcon-context-nativequit-final-nuwesksr/verified-receipts.json.
This is actual native transport/gameplay/save behavior, not a full graphical
client prediction/snapshot parser or every save dialect/identity/hub proof.
The ten discovery/admission cases also pass on production079f4431 under
FastGames/qsvr-rcon-context-final-rnccijr2/discovery.

Failed observer runs remain retained: native enum/vector print parsing, binary
rule JSON representation, one relocated missing private save, and a shutdown
wait exactly equal to native NET_SendToAll's5-second window without peer ACK
pumping. The final observer encodes only binary rule data as hex, copies the
actual save and pumps the existing native peer during bounded ordinary quit.
No production workaround or weakened gameplay/exit predicate accompanies these
observer corrections. Successful RCON map/reconnect and expected error exits
remain a separate final lifecycle check.

## Current RCON world replacement and error exits

Production079f4431 passes three actual native dedicated lifecycle cases.
The positive case completes connected public15 signon/gameplay, sends one
admitted authenticated map-start command, observes the native empty replacement
world, retires its old UDP peer, binds a distinct fresh peer and completes a
second real signon/gameplay plus interleaved status/discovery/rule/RCON controls.
Both phases receive all five client reliable ACKs. Ordinary stdin quit exits0,
with actual native shutdown ACKs pumped by the existing peer handler.

Failed map and failed changelevel commands are sent once in separate connected
native processes. Both exit1 and ACK shutdown, with no nested-QCVM error. The
map failure flushes its actual Couldn't-spawn-server diagnostic in the RCON
response before the inactive-server Host_EndGame guard; changelevel takes the
ordinary Host_Error path and names its missing map. Native abort1 and quit0 are
different expected outcomes. Main verifies all receipts and the exact engine
SHA against production079f4431. No listen-server recovery claim follows yet.

Receipts: FastGames/qsvr-rcon-lifecycle-requalified-g24kbgh_/main-verified.json and
qualification-20261002-225523-734099/qualification.json. Luna's initial probe
mistakenly looked up server_protocol in a result dictionary and required
Host_Error for both commands. Main corrects the observation to the existing
module field and the actual distinct native abort diagnostics; all original
wire/ACK/movement/map/exit predicates remain. Initial failure receipts remain
under qsvr-rcon-lifecycle-final-xb247oxg. No production changes were necessary.

## Available CPU desktop gameplay and terminal outcome

The current native079f4431 fork client and unchanged public vkQuake desktop
peer each connect independently to the current native dedicated server through
real UDP with public negotiation. Each reaches signon4, uses held native forward/
attack input, moves266.981units, consumes four shells, advances ACKs and settles.
Both client inferiors and both dedicated servers exit0 through native quit;
no recurring public allocator abort occurs in this bounded desktop run.
D02's historical mixed-XR failure is retained separately and its unavailable
mixed-XR reproduction is excluded by the user; no attribution is invented.

Signed private Mesa CPU Vulkan driver, new private Xvfb, child-only environment,
320x240 and8Hz render cap; no hardware GPU/runtime/system changes or measurements.
The existing public gameplay probe's predicates are unchanged. A private test
extension queues native quit and observes GDB's inferior-exited event; main
corrects its initial internal-breakpoint disable to use actual Python breakpoint
objects. The initial run proved gameplay then stopped on an observer breakpoint,
not a production shutdown defect, and remains retained. No production edits.

Main verifies both result/terminal receipts and source/binary hashes:
FastGames/qsvr-cpu-desktop-nativequit-f7_wf0n_/main-verified.json and result.json;
initial observer failure root qsvr-cpu-desktop-final-e029gx62. This qualifies
ordinary desktop gameplay/quit and public compatibility, not XR lifecycle,
client between-send prediction, all desktop content or uninspected output.

## Available desktop content and built-in demos

Current native079f4431 executes three CPU-rendered desktop profiles with actual
licensed id1/Hipnotic/Rogue packs linked read-only into private game directories.
Each completes signon, native held-input movement/firing/release, ordinary map
replacement (e1m1→start, start→hip1m1, start→r1m1), native menu frames and quit0.
No copied graphics implementation or hardware driver is used.

The id1 profile records a real native desktop demo, stops recording, plays it
through the native demo reader, pauses with decoder time unchanged, and seeks
forward0.5seconds with real message time advancing1.8→2.4 before stopping playback.
It then loads the next map and exits normally. All profiles verify postcfg
sensitivity4.125 after initial config2.75, and the ordinary quit-time native
vkQuake.cfg writer preserves4.125. An attempted observer-only writeconfig command
is unsupported by both this engine and native vkQuake; it proves nothing and
requires no new command. Acceptance uses the actual native config files.

Main verifies every normal terminal/state receipt, config value and demo file,
then inspects all three real320x240 menu/HUD images for native readable menu and
underlying world/HUD output. This is finite desktop content/record-play-pause-seek/
config/menu acceptance, not all paths/catalogue/assets/cache/classic-quads.
No VR demo or campaign/benchmark claim. All owned Xvfb/game processes are stopped.
Receipts: FastGames/qsvr-cpu-content-final-vjiawsyf/main-verified.json, result.json,
per-profile state receipts and PNGs. Shipping input/source remains079f4431.

## Listen-server RCON recovery and packaged startup

Available CPU-rendered native079f4431 listen-server recovery passes. An actual
authenticated UDP changelevel-missing command enters native Host_Error with
borrowed SSQC suspended and redirect active. Ordinary unwind returns to the
main frame with server/signon retired, VM clear and redirect clear; the actual
RCON response contains the missing-map error. Ordinary map-e1m1 starts a fresh
world/signon, a new RCON echo succeeds, and native quit exits0. The Debug
Sys_DebugBreak SIGTRAP is observed at its expected Host_Error stack and resumed
without delivering a fatal signal. Main independently verifies each predicate.
No listen recovery is inferred merely from dedicated exit1.

Receipt: FastGames/qsvr-cpu-listen-postcfg-_232esbw/main-verified.json and result.json.
Earlier private observers are retained: oversized reconstructed native argv
and uninitialized private RCON setting prevented the intended server/error path.
The final observer verifies actual native231-byte cmdline, real active listen
server and postcfg-initialized password before sending the single error command.
This is observer setup correction; no source setting/default was changed.

The fresh079f4431 Linux shipping package also loads e1m1, reports native status
and quits0 using its packaged relative runtime search paths with inherited
LD_LIBRARY_PATH removed from that child. No GPU or external SDK loader override.
Receipt: FastGames/qsvr-packaged-linux-079-startup-xl1m2js5/result.json.

## Current079 platform inventory reconciliation

Main verifies the exact079f4431 source archive SHA
38bb21884f6a33150024bec8d2abcc189d9038435b2228665f29566ec99c9b44 and all three
changed shipping files against the current worktree. Fresh Linux653 and native
ARM650 inventory entries match actual bytes/symlink targets; each package has45
ELF records and matching079revision/archive. Native builds/stage/verify all exit0.
Windows Debug MSBuild/staging succeeds. Main compares all13PE files to native
artifact receipts and both license files to the accepted unchanged0f277 cohort.
All16staged files are hashed in windows/main-debug-inventory.json; the remaining
PDB hash is recorded, without an independent PE/PDB GUID-match claim.
Root: FastGames/qsvr-platform-discovery-079f4431-20261002,
main-inventory-current.json and platform receipts.

Windows Release fails twice during LTCG Generating-code with MSVC C1001/LNK1000
in unchanged gl_model.c, using HostX86/x64 c2.dll. Main does not classify a
runnable build failure as unavailable merely because it failed. One new bounded
build selects the installed x64-host tools using Microsoft's documented
[PreferredToolArchitecture property](https://learn.microsoft.com/en-us/cpp/build/reference/msbuild-visual-cpp-overview?view=msvc-170),
preserving source, pinned dependencies and Release/LTCG optimizations. It is an
explicit toolchain diagnostic, not an observation-timeout restart or a presumed
memory-pressure diagnosis. Its result remains pending. No system installation,
source workaround or deployment.


## Attainable metadata lifecycle and loaded AUTOCVAR — current production source

Luna Lovelace implemented the opt-in lifecycle fixture; main reviewed its native
producer/parser and real loaded QC calls, and checked final logs. Private receipts:
`FastGames/qsvr-metadata-v02v03-20261002-wzSp80`, `main-verified.json` binds reviewed
file and log hashes. Default-compatible and opt-in native links both succeed.
The two opt-in runs exit0: QSMI reaches signon4, changes metadata mid-signon,
16 occupied slots publish complete envelopes over two reliable packets, QC retires
and reuses a slot, empty-full reset and subsequent update clear stale keys.
Five live refused operations preserve queued bytes, flags and the bound SSQC
AUTOCVAR; five admitted retries update native state, with exact AUTOCVAR call/value
checks where applicable. PREDINFO downgrade initializes serverinfo after two
empty snapshots and62updates. Existing six-profile acceptance remains credited.
This proves the local reliable native transport and loaded SSQC boundary; it is
not external UDP or graphical outcome proof. Generated SSQC recipe is preserved
as `tests/make_metadata_lifecycle_qc_fixture.py`, reusing the existing QC assembler.
Production files are unchanged. Failed observer attempts remain in the receipt
root; only final run logs count as passes.


## Initialized native input lifecycle and gameplay — current source079f4431

Main reuses the existing `vr_input_lifecycle_smoke.gdb` and
`vr_input_gameplay_smoke.gdb` in separate isolated CPU-rendered desktop profiles.
Both run0 and ordinary native quit0. Lifecycles preserve original predicates:
ALT-bound key ownership/releases, shared keys, handedness changes, focus and
menu/context neutral-rearm, same-sample menu transitions and real native binding
capture. Gameplay supplies only the completed action-frame boundary, then native
keys/Cbuf/movement/transport/loaded stock QC produce displacement and ammunition
consumption. Focus loss releases forward/attack; held controls on return remain
released until a fresh edge, which fires again. No kbutton/usercmd/packet fields
are manufactured in these input probes. Renderer uses the private signed CPU
Vulkan driver and Xvfb, with sound disabled; no XR runtime/device proof follows.
Private script adaptations use short native startup args and native quit rather
than killing a successful inferior. Receipt `FastGames/qsvr-cpu-native-input-hq06s44u/
main-verified.json` records exact scripts, result and log hashes. Earlier input
component and actual6DoF proofs remain credited at their recorded scope.


## Native calibration filesystem, restart and authored AD-mod defaults

Current079f4431 native engine: four CPU-rendered cases run0/ordinary native quit0.
Main reviews results and hashes scripts/logs/files in `FastGames/qsvr-cpu-calibration-final-mqr4n9z4/
main-verified.json`. An isolated authored schema loads through the real search path;
controlled public `VR_WeaponCalibrationApplySchema` changes the enhanced held
X1.25→9.25. The ordinary `vrweaponsave` command runs through native Cbuf/host,
COM_WriteFile writes the active-game file, COM_LoadFile/reload reads the same9.25
held and4/5/6muzzle, and a fresh native process reads those exact values. The
selected viewmodel is native MD5: its enhanced keys are written while authored
classic keys remain intact; no `mp_` keys are emitted. It does not force classic
geometry or alter renderer/model defaults. Calibration accept/cancel via tracking
and projectile placement are outside this filesystem witness.

Two separate processes load the installed authored q30a1024 start map/QC using
read-only asset links, with no private or installed `vr_weapons.txt` carried over.
The default fallback is AD: shotgun held1.5/1.7/17.5, scale.33, muzzle0/0/17.5.
Native AD preset/reload and process restart preserve these values. Original game
assets/configuration/calibration are not written. This checks the existing AD
root rule, not new mod-specific behavior.

Failed private observer attempts remain retained: optimized-out private helper,
transient direct-console token observation, classic-format expectation against
the actual MD5 model, and an incorrectly escaped cleanup string. Final checks
use registered/native public owners, normal command-buffer execution and actual
selected model format; production source was not changed for these attempts.


## Loaded native CSQC entity lifecycle — captured transport

Luna Hubble adds one bounded native SSQC/CSQC witness; main reviews sender/parser,
actual loaded callback and ACK ownership and checks final log markers. All six
markers pass/run0 against the assertion-enabled225-object graph. Actual
SendEntity payloads are56bytes, forcing native split datagrams under the128byte
bound. Create/update callback order and values match the native current edicts;
withheld sequence5 requeues entity182, recovered payload21. Native enablecsqc
command disables/re-enables the existing mapping; withheld removal sequence12
replays at14, exactly one remove runs and removewait clears. Native allocator
reuses the acknowledged ID with exactly one new callback/payload41.

`FastGames/qsvr-csqc-entity-native-sqruypeq/main-verified.json` records actual
reviewed source/log/graph hashes; graph hash is not an engine-binary hash. Four
link owners are replaced only for the fixture, with production sender/frame/
parser and native clc_ackframe consumers. Unreliable bytes/sequence delivery is
captured under explicit loss. A fixture-supplied svc_nop checks parser framing;
this is not a real typed cursor/weapon producer, two actual sockets or graphical
CSQC proof. The separate actual UDP gameplay proof stays credited at its scope.

## V06 observed co-op saved-reference failure — review before repair

Luna Archimedes' opt-in loaded stock e1m3 co-op case passes native collision/
friendly-fire/telefrag policy, shared key/weapon ownership without copied ammo,
exact-once target counter, real two-identity v7 save, reverse reconnect identity/
inventory/queue reset and cooldown/near-player helper. It then aborts: saved
world.enemy edict2 names beta, who reconnects as1; after alpha reuses2 the saved
reference resolves to alpha. The current payload-only restore/name match does
not relocate typed references. Main reads source and failure in
`FastGames/qsvr-protected-native-72o89z1i/profiles/qsvr-local-load-coop-lifecycle-d97i5l26/native-run.log`.
This is an observed software failure attached to existing V06, not a new feature
or unavailable check. The [verified design brief](coop-save-reference-final-2.0-review-brief.md)
asks the same local Astra to verify the premise and select the smallest native
adapter before a production change. Beta's prepared endpoint remains explicit;
no full two-socket co-op claim. Authored KEX/hub inputs were not found by the
finite installed-asset lookup and their authored-input subcases are excluded.


## Real negotiated UDP voice and SDL playback callback — current079f4431

Luna Lovelace's private attempt05 passes actual sender/server/receiver UDP voice,
loaded client/server negotiation and actual SDL dummy capture/playback. Main
reads observer/audit/result boundaries and independently runs offline receipt
validation0:261actual byte/sample artifacts. Matching datagrams and Opus payloads
pass clientCL_SendMove→server acceptance/relay→clientCL_ParseVoicePacket→Opus
nonzero PCM→resampled receiver rings→paint_audio on the SDL SDLAudioP15 thread→
accepted SDL_PutAudioStreamData. Exact callback prefix mixing and first decode's
960mono48kHz samples→882stereo44.1kHz frames are checked from captured bytes.

Baseline has10complete payload chains/9600decoded samples/15callback puts/61440
callback bytes; after map change8chains/8640samples/10puts/40960bytes; receiver
reconnect8chains/8640samples/12puts/49152bytes. All negotiate voice version1,
signon4, native datagram driver1/UDP landriver0 and capable peers. Map change and
disconnect clear all16receiver generation/jitter/ring/talking states and reset
all16Opus decoders before subsequent successful delivery. All native engines
quit0. Separate private Xvfb displays preserve playback focus.

`FastGames/qsvr-voice-udp-sdl-v20v21-i9aT6L1Z/main-verified.json` binds scripts,
validation and attempt05 states/results. Controlled PCM replaces actual SDL
dummy capture frames; consent/PTT are explicit native test KeyEvents. No physical
mic/playback, XR, throughput/latency or music EOF claim. Attempts01–04 preserve
observer/consent/focus failures, without a production repair.

## Native desktop cache restart/corruption and dense classic particles

Main uses current079f4431 native engine, private signed CPU Vulkan driver and
isolated Xvfb. Four fresh/valid/corrupt/repaired process cases each run0/native
quit0. Observed vkCreatePipelineCache receives empty/32byte/empty/32byte initial
payloads respectively; actual native cache handle is nonzero. Real native quit
writes the VKPC envelope, the next process consumes it, damaged magic is rejected
and overwritten with a valid cache, then a fourth process consumes the repaired
file. The CPU driver returns a32byte header payload: this proves cache lifecycle
and fallback, not a cached-shader performance improvement.

With `-particles32768`, actual native index-buffer staging contains uint32quad
indices at0,16383,16384,20479,32767. Quad16384 starts65536 and quad32767 reaches
131071, so neither wraps to the first vertices. Twenty actual classic
R_ParticleExplosion calls produce20480active particles; real R_DrawParticlesFaces
binds the native particle buffer as VK_INDEX_TYPE_UINT32 and issues122880indices.
Native rendering/shutdown completes without a production change. This is the
classic-particle C22owner, not unrelated menu quads or a benchmark. Stereo output
and physical GPU performance are excluded unavailable outcomes.

`FastGames/qsvr-cpu-cache-particles-_cpa8cu1/main-verified.json` binds actual
runner/GDB/log/results and retained valid/damaged/saved cache bytes. A private
runner string-generation syntax error was corrected before any inferior started;
no test predicate or production source changed.
