# Native voice VAD final qualification plan

2026-10-02. F08 remains open. Reuse the existing assertion-enabled dedicated voice PCM fixture, its included production voice.c, native VAD/Opus/client queue/server relay and SDL dummy capture. No new voice protocol, capture abstraction, VAD implementation or production test hooks. Main inspected Voice_EncodeCaptureFrame, Voice_ResetTransmit, Voice_CaptureDiscontinuity, Voice_VADProcessFrame and the four-packet outgoing capacity.

Add one optional -vad fixture slice. Quiet warmup followed by two loud frames must produce actual encoded preroll and onset packets: oldest first, one START, continuous sequence/timestamps, one talkspurt. Independently decode the four packets with Opus to observe quiet then voiced content. Drain through actual CL_SendMove/GapDeliver before further frames; do not overwrite queue counts. Twelve quiet frames exercise native hangover and one payload-free END; additional quiet emits nothing. Observe nonzero/decaying public input meter and transmitting state.

Exercise PTT with the real bound key, demonstrating immediate onset without preroll and key-game eligibility. Directly call the existing discontinuity owner for non-failure and failure cases: queued stale speech retirement, END, reset preroll/VAD/meter, held-key policy, failed capture closure and retry deadline, explicit dummy capture recovery. This qualifies reset behavior, not actual device-read error detection or hardware retry timing. Restore PTT mode, gain/sensitivity/key destination and the sender state through native owners. Retire produced server relay data through existing owners, preserving subsequent PCM/recovery/routing checks.

Ownership: one Luna/xhigh worker may edit only tests/voice_vad_native_fixture.h and the include, optional call and completion marker in tests/voice_pcm_native_fixture.c. No production edits; main owns this plan, private build/run receipts, integration and checklist. Maximum 230 new C lines; report missing evidence rather than widening scope. No builds/runtimes/nested delegation. Atomic writes after checking at least16 MB root free. Main verifies complete source before one consolidated current native compile/link/run with -vad -routing -recovery, dummy audio, dedicated/no UDP/no sound/no Steam API and fresh private preferences. No graphics/OpenXR/runtime/device or user microphone acquisition. Recorded observables supplement counters. F08 spatial/music/active-XR boundaries and full Windows compilation/link remain separate.

Senior disposition: native production code iterates the preroll ring oldest
first; the executed oracle distinguishes quiet/quiet/voiced/voiced content
classes and continuous wire metadata. It does not uniquely identify each
source frame within the same class. Keep that explicit qualification limit;
do not claim full per-frame chronology from this run. F08 remains open.
