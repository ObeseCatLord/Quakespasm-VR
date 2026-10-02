# Native fallback spatial voice results

2026-10-02. Frozen F08 subset; [before-code plan](voice-fallback-final-2.0-plan.md).
No production changes. Luna/xhigh wrote the132-line optional header, source-only;
main verified effective context, inspected full code/native owner, closed worker
and integrated optional -spatial-fallback in the existing fixture/builder.

Build/run/completion/input-stability all0 with all eight markers and normal
Host_Shutdown in one -vad -routing -recovery -budgets -fatal-send -client-send
-spatial-fallback process. Prior seven native cases, including held-PTT client
fatal close, pass together with the new fallback cases on this exact fixture.
Assertion-enabled debugoptimized SDL3/voice/Opus/codecs/CURL native object graph;
Steam Audio disabled. Private receipts:
/home/obesecatlord/FastGames/qsvr-voice-fallback-t3f5znul.

Each case sends real native producer speech/END through actual client writer,
server parser/relay, full receiver parser, jitter/Opus decode and native stereo
mixer. Decoded and mixed signal are nonzero; each PCM ring is consumed.
Actual stock player qmodel presence and prepared entity position/current-or-stale
msgtime plus public listener API are acoustic inputs. Output PCM/channel gain is
never assigned. Independent within-packet channel energies avoid comparing
amplitudes across evolving decoder history or copying production gain formulas.

| Prepared input | Observed native mixer output |
| --- | --- |
| Current nearby source on listener right | Right-channel absolute sample energy exceeds left; both nonzero. |
| Same nearby source, reversed listener right | Left energy exceeds right. |
| Listener origin moved beyond source to its opposite side | Left energy exceeds right with original right vector. |
| Source beyond configured spatial distance | Nonzero centered radio output, sample-wise equal stereo channels. |
| Missing source model | Nonzero equal-channel radio fallback. |
| Stale source model msgtime | Nonzero equal-channel radio fallback. |

Prepared entity model/origin/msgtime and public listener vectors restore to their
original values; num_entities/preferences/speaker volume/mute and endpoint
selection stay unchanged. Native sending advances source transport state with
released PTT/empty packet queue; the following original pending PCM/jitter/held
PTT/encoded-send fatal/reset witnesses and normal cleanup still pass. No new
renderer, listener/queue owner, codec, transport or test framework.

348 actual native dependency/link/PCH/executable/runner/licensed-pak0 inputs
are byte-stable before/after execution, including217 linked objects and1PCH.
Ten source hashes independently match current files. Executable SHA256:
7671b23e6642b4ce03f4bffd41b23a421cc3555d8ae490fd926f3d2be0ea6495.
Actual argv/logs/source hashes/pre/post inputs and provenance summary retained.
Linked licensed pak0 is used only for reading; no enforced read-only mount claim.
Fresh preferences/dummy audio/dedicated/no UDP/no sound/no Steam API; no GPU,
OpenXR/headset/hardware microphone/deployed server/user configuration changes.

Main integration review covers this bounded relative-channel fixture; it is not
a separate final Astra integration signoff. Existing client-send design was
reviewed by local Astra and its required held-PTT correction is in this run.
Prepared acoustic positions/public listener call do not certify live network
entity updates, native host/XR listener publication or listening quality.
Steam Audio/HRTF, loops/callback/room-worker/music/device-read failure/retry and
remaining F08/F10 acceptance are distinct. No performance measurement/claim;
no fresh common-object shipping rebuild or Windows artifact claim.
