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
Explicit lower-resolution/command-rate software qualification remains underway;
original thresholds remain unchanged. Receipts: FastGames/qsvr-final-cpu-vulkan-
bdm_i5vc and qsvr-final-gpu-resumed-khdhlxgs. The driver is private and selected
only in child environments; no system installation or global runtime setting.

## Still running or awaiting final execution

Final shipping artifacts are underway;
protected-output test inputs/capture/checker are in preparation. Remaining V rows,
D01/D02 connected oracle/terminal evidence and final local Astra integration
signoff stay open until their actual outcomes are recorded. These results must
not silently substitute component passes for those completion boundaries.
