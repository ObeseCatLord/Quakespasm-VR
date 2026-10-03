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
