# Native VAD/preroll and interruption qualification

2026-10-02. No production change. Reuse the existing native voice PCM fixture and builder, actual VAD/Opus/client writer/server parser/relay/jitter/mixer and dummy capture. [Before-code plan](voice-vad-final-2.0-plan.md), [verified senior brief](voice-vad-final-2.0-review-brief.md). Local Astra/xhigh review is complete with the dispositions below; this is not final F08/F10 signoff.

The current assertion-enabled debugoptimized Linux native target has no pending production jobs. SDL3/voice/Opus enabled; Steam Audio disabled. Combined -vad -routing -recovery build0/run0/completion0, all four markers and normal native shutdown. Actual builder/compiler/link/run argv, source hashes, logs and exits are retained in the private FastGames qsvr-voice-vad-jmxwjjq8 profile. Fresh XDG preferences; licensed pak0 is linked read-only. No Vulkan/OpenXR/device/headset/user microphone, deployed server or user settings changes.

| Boundary | Observed native behavior |
| --- | --- |
| VAD onset | Four quiet frames and two loud frames through production capture-frame encoding yield the four-packet queue. Native sending/parsing preserves every serialized field and active payload. Exactly one START on the leading packet; continuous sequence/timestamps and one talkspurt. Independent Opus decoding observes two quiet frames followed by two voiced frames, proving onset content-class order. Same-class source frames are not uniquely identified by this oracle. |
| Silence and meter | Twelve quiet frames yield eleven audio packets followed by one payload-free END. Public transmitting state closes; three more quiet frames emit nothing. Public input meter rises on controlled speech and decays during silence. |
| PTT | Real bound key is denied in key_console, accepted in key_game. PTT sends immediately with one START/audio packet, bypassing VAD preroll. Released key produces END. |
| Non-failed interruption | With held PTT and three queued speech packets, existing discontinuity owner retires them and leaves only END. Capture stays ready; held-key policy remains. Preroll/VAD/meter reset. |
| Failed interruption | Existing failure owner retires queued speech to END, closes dummy capture, clears capture-ready and schedules the existing ten-second retry deadline. Held-key policy remains; explicit forced refresh reopens the real dummy stream and subsequent PTT speech/END traverses the native transport. |
| Integration | Native relay/parser/jitter/mixer retires produced audio; actual client snapshots preserve sender/receiver state. Existing PCM/mute, routing/preferences, pressure/gain/loss/reorder/generation/reset/shutdown checks pass in the same process afterward. Mode/transmit/gain/sensitivity/binding/key destination are restored. |

Luna/xhigh wrote the test slice with disjoint ownership and no runtime execution. Main inspected the complete source, repaired reset/receiver snapshots and sender netcon/key-game restoration, then used the existing five-owner fixture builder. Initial build0/run-6 failed whole-struct memcmp. Retained separately under initial-oracle-failure; the wire protocol omits C padding/unused payload capacity. Main replaced that oracle with exact sequence/timestamp/talkspurt/flags/length and active payload comparison; the final run passes these stronger protocol-specific observations. No production workaround.

Direct discontinuity calls qualify reset behavior, not the actual device-read failure/backlog detection path, real-time retry, physical microphone selection or hardware disconnect. Core client resources/signon are prepared and transport sends are captured successfully by inherited wrappers; independent live sockets remain unqualified. Controlled PCM/DMA and one shared voice owner do not establish listening quality, real network cadence, active XR profile capture or spatial/music behavior. Remaining frozen F08 distinctions and full Windows/ARM artifact reconciliation stay open. No performance claim.

## Senior review dispositions

Main verified the effective local review context gpt-6-astra/xhigh. Reviewer
verified current source, all six input hashes, exits and four completion
markers; read-only, no additional runtime. Main independently checked the
referenced producer encoding, serialization, reset snapshots and payload-copy
owner. No P0/P1 findings; one P2 qualification limitation adopted.

| Recommendation | Main disposition |
| --- | --- |
| Energy classes do not uniquely prove oldest-first source-frame chronology | Adopted. The qualified result is quiet/quiet/voiced/voiced content-class order with exact wire fields, leading START and continuity. Production ring iteration is inspected but a swap within a class could evade this oracle. Do not claim unique per-frame chronology or close all F08. |
| Retain native state/resource boundaries | Accepted. Reset results are saved before sending; relay jitter copies stack-local payload; receiver snapshot follows mixing; sender/netcon and final snapshot are restored. No new lifetime owner. |
| Preserve direct-reset and dummy-reopen exclusions | Accepted. No actual device-read/hardware failure detection, elapsed retry, physical input delivery, HUD rendering or active XR certification. |
| Optional redundant final release/stop deletion | Deferred. Explicit fixture retirement remains harmless and was included in the accepted native run; no production cleanup or architecture change is needed. |

The review did not require a source change after the passing run; no redundant
rebuild/rerun. Final F10 integration review and Windows full compilation/link
remain outstanding.
