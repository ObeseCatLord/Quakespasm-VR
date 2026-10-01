# F01 connected map and slot lifecycle results

2026-10-01. Current preliminary host binary through production aa828629,
private native IPv4 UDP stock-coop server, desktop and simulated OpenXR clients.
The complete optional lifecycle run passes, retaining all original gameplay
probes. No production changes were needed. Separate disposable profiles and
read-only licensed pak0/weapon-preset links; no user settings/server deployment.
HMD remains runtime-owned and checked unchanged; controller/key inputs remain
software-controlled. This is behavior evidence, not headset/performance proof.

| Boundary | Actual private result |
| --- | --- |
| Original desktop/VR probes | Both pass movement/fire/ACK and selected rendered-prediction assertions before handoff |
| Native map change | e1m1→e1m2 through dedicated server stdin, both signon4 with named owners |
| Movement retirement | Actual CL_ClearState completion clears ACK/sent, pending command time, all command-ring entries and private snapshot validity on both clients |
| Fresh transport | Desktop socket send/receive284/158→293/165, VR23/89→32/124 across map reset; new coherent private ACK/owner |
| Disconnect and slot reuse | Desktop native disconnected/netconNULL; VR sees ['', 'VR'], then ['Replacement', 'VR']; new desktop owns exactly retired slot0/entity1 |
| Renewed gameplay | VR stationary shells25→minimum23, replacement21→minimum19; then actual moving/fire commands, advancing ACKs and authoritative movement |
| Mutual observation | Shared stop/observe phase, both received peer positions match authoritative owner samples (0/0units) |
| Exit and rendering lifetime | All three original probes and lifecycle JSON statuses/markers pass; native clients/server exit0; four logs contain no VUID or synchronization hazards |

The lifecycle extends the existing runner/probes and reuses their controller
injector, native commands/server console, transport, QC, parser and renderer.
No assigned commands/body/ammo, transport replacement or production bridge.
Original Desktop retains native cadence; existing VR diagnostic10Hz and new
Replacement diagnostic30Hz permit bounded debugger/presentation observation.
This does not establish default-VR-cadence performance.

Main reviewed Luna xhigh helper/hook output and corrected fixture defects at
the existing boundaries: slot-ordered identities, public-layout access, fresh
wire observations, initial phase readiness, quit breakpoint lifetime, ammo
pickup masking and asynchronous pose sampling. All assertions survive; no
product behavior was changed to pass a fixture. The final helper explicitly
retires original Python breakpoints and requires actual native exit events.
[Before-code plan and evidence-driven adjustments](connected-lifecycle-final-2.0-plan.md).

Private passing evidence: connected-lifecycle-current-handoff-repeat with
server/desktop/VR/replacement logs, three original results, three lifecycle
sidecars and aggregate connected-crossplay-result.json. Earlier failed roots
initial/cadence/final/observed/stationary/handoff remain separate. The handoff
trial failed BEFORE lifecycle on the original rendered movement threshold:
90/90 successful owner-matched replays,62stable pairs but maximum displacement
0.119units below0.25. A subsequent unchanged run passed; this intermittent
outcome remains unresolved, not repaired or hidden.

## Unchanged public desktop alongside private XR

The existing unchanged vkQuake4bc898f2 baseline also passes the complete
lifecycle as both Desktop and Replacement against the current server/XR.
Public peers remain dialect0/PREDINFO; XR remains private with selected
prediction. Both original probes and replacement's original public movement/
firing/ACK assertions pass. Native map change clears public command-ring/ACK
state without assuming private fields. VR observes old slot retirement and
Replacement in exactly slot0, then both clients move/fire and receive coherent
peer positions (0/0units after settling). VR stationary shells25→minimum23,
replacement21→minimum19. All three original/sidecar statuses/markers and
clients/server exit0; four logs contain no VUID or synchronization hazards.

Private passing evidence: connected-lifecycle-current-public-ready, with the
same detailed result/log set. Initial public and public-diagnostic failed roots
remain: the original public settled-distance gate failed before lifecycle on
the first run, while the second revealed a helper admission timing assumption
(peer name absent when XR completed its original probe). The helper now waits
for actual signon4 and both exact names under its existing bounded pump/deadline
before sampling. Structured desktop samples retain their already-observed
vectors so future distance failures can be diagnosed without changing inputs
or thresholds. The original intermittent distance outcome remains unresolved.

## Focused senior-review correction and affected reruns

Verified local Astra xhigh identified missing post-replacement XR prediction
state assertions. Main spot-checked and adopted the finding, reusing original
prediction_sample/require_selected_prediction_state at replacement and final
observation. The helper records real permission, authority, ACK and snapshot
owner/state. [Review dispositions](connected-lifecycle-final-2.0-plan.md).

The private-selected rerun passes all original/lifecycle checks, native client/
server exits0 and four clean validation logs. Both new XR checkpoints have
permissiontrue, authority2, snapshot valid with matching ACK38/108 and owner1.
The retired/reused slot is selected dynamically, not assumed to be slot0.
Mutual pose errors0.240/0units. Earlier detailed table describes the first
accepted private run; this is the affected final-helper rerun.

The public-selected rerun passes all original gameplay and reaches both new
XR state assertions (permissiontrue, authority2, matching snapshotACK43/113
and owner2), native map/retirement/reused-slot and renewed gameplay/received
poses (0/0units). It then FAILS aggregate acceptance: unchanged public Desktop
reports 'corrupted double-linked list' and SIGABRT during native quit. No normal
exit is claimed for that client, remaining client/server shutdown is interrupted
by owned-run cleanup. Four logs contain no VUID/synchronization hazards.
This matches a previously documented untouched-baseline failure symptom, but
this run does not establish the corruption owner/root cause. The read-only
baseline is untouched. Do not call this newest public-selected run a full pass;
retain the earlier public-ready normal-exit proof and this precisely narrower
post-replacement state evidence separately.

Private evidence: connected-lifecycle-current-private-selected and
connected-lifecycle-current-public-selected, complete original/lifecycle JSON
and native logs. Runner success alone omits validation acceptance; main's
independent four-log inspection is the evidence for each stated clean run.
The focused review found no further concrete helper/role/layout/reset/slot/
input/quit assertion issue; it was not overall F10 signoff.

This establishes the private and earlier unchanged-public native map/reset/
disconnect/reused-slot vertical proofs and the stated post-replacement XR
state boundaries. The newest unchanged-public full-shutdown rerun is failed. Loss/reorder/split
snapshots, IPv6 and remaining metadata lifecycle/refusal boundaries remain
F01 work. Packaged Linux/native ARM refresh and overall senior acceptance
remain F10 work; no whole-F01 or whole-goal completion is claimed.
