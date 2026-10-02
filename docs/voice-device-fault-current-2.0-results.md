# Native capture API failure and retry results

2026-10-02. Verified effective gpt-6-luna/xhigh completes the interrupted fixture;
main reviews all three changed test files and checks recorded source/receipt
hashes. [Before-code plan](voice-device-fault-final-2.0-plan.md). Existing native
Voice_Frame/capture/discontinuity/retry/Key_Event/Opus/queue owners are reused.
No production source, device/provider policy, protocol or DSP change.

For both SDL3 available-negative and read-negative failures, wrappers are armed
only for the exact actual dummy capture stream. All unarmed calls forward to
real SDL. Public physical Key_Event PTT plus actual generated960sample PCM primes
START payload, sender, positive meter, VAD and preroll. Public Voice_Frame then
closes native capture and clears sender/meter/VAD/preroll/old payload, retaining
permission/wanted capture/held PTT and exactly one END-only packet. Native default
recording route remains selected; no hardware recording is attempted.

Prepared realtime proves no open before the existing10second deadline, exactly
one injected failed open at that deadline, another10second backoff, and a real
SDL dummy open at the second deadline. Permission/route remain; release and new
physical PTT/real PCM after reopen produces fresh START payload and positive
meter. Binding/menu/session/timing state is restored through existing owners;
wrappers disarm and all prior fixture cases run afterward. These are software
API failures and prepared game-clock timing, not real device-removal or wall-time
claims. Captured transport/prepared peer state is not live socket delivery.

Combined current run compile/link/run0 with all13 markers exactly once, including
all12 prior voice/transport/spatial/HRTF/music/room cases. All397 observed inputs
match before/after. Main independently verifies29 recorded repository sources
still match their receipt hashes and checks all marker counts and statuses.
Private receipts: FastGames/qsvr-audio-device-fault-HbG6IZ, exit-status.json,
source-provenance.json, sdk-remap-provenance.json, pre/post-run-inputs.json,
main-verified-summary.json and compile/link/run argv/logs. Compiler/helper path
and an attempted private key-array access failures are retained; final native
Key_Event reuse avoids reaching that private state.

Former SDK files under /tmp were lost across reboot. Private compile/link argv
remap to the freshly built official Steam Audio4.8.1 dependencies in
FastGames/qsvr-linux-0bd4-6fadlwzp/native/deps. All3 public headers byte-match prior
receipts. Current libphonon/libpffft hashes differ from prior library receipts
and are explicitly recorded; they remain stable through this run. The existing
native engine object graph is borrowed, not rebuilt/replaced. No shipping recipe,
system dependency, copied codec or production fallback is added for this test.

This closes the bounded SDL3 capture availability/read fault and retry boundary
in F08. Physical microphone/removal/OS permission behavior, active headset profile,
remaining voice map/live-delivery boundaries and whole F08/F10 acceptance remain
distinct. No whole-goal signoff follows.
