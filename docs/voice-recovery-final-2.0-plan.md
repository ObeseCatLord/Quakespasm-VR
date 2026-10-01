# F08 native voice recovery: before-code plan

2026-10-01. Extend the accepted directed PCM component proof without replacing
any production owner. Current native voice_jitter, Voice_ReceivePacket, producer,
cl_main/cl_input/cl_parse and sv_main/sv_user already own reordering, concealment,
monotonic generations, queue pressure, relay expiry and rate limits. The inherited
OpenVR voice.c uses the same queued/Opus/PCM architecture; retain its adapters.
Missing evidence does not justify a new codec/transport/provider or general test
framework. Existing dummy guard, private preferences and resource-through-shutdown
lifetime stay mandatory. No microphone recording, deployed server or asset writes.

Minimal route: reuse the fixture and exact primary Meson object graph. Captured
bytes are delivered out of order/omitted/replayed at its existing transport seam;
never assign voice protocol/capability/packet/generation/decoded-PCM values.
Inherited core client protocol/resources, controlled-successful send wrapper,
synthetic capture PCM/DMA and controlled clock remain explicit
component seams. No connected timing, hardware/HRTF/full F08 claim.

## Finite cases and ownership

1. Main: actual numeric-slot gain command zero suppresses PCM only after actual
   Opus decode is shown nonzero; restoring one yields nonzero mixed output.
2. Main: retain an actual relayed datagram, retire/recreate source via native
   qsocket/drop/SpawnPeer/negotiation/resource owners, native warmup and send new
   voice. Require newer server generation and removal of old PCM/jitter, then
   replay old captured bytes through full client parser and require unchanged
   new generation/jitter/ring. Decode/mix renewed audio. Keep prepared resources
   alive until global aliases stop using them; no invented generation values.
3. Main: generate three contiguous encoded frames in one native talkspurt and
   native END, capture their actual relay packets, deliver reordered before
   deadline and omit a middle packet. Observe actual jitter ordering and actual
   Voice_Frame/Opus output/concealment after controlled deadlines. Do not drain
   jitter through a second decoder or synthesize packets. Packet loss is at the
   captured component boundary, not a real socket latency claim.
4. Luna xhigh: client queue pressure via real capture-frame producer while sends
   are held. Require bounded count and freshest actual sequences retained;
   native StopTransmit/END/send retires that test pressure.
5. Luna xhigh: native captured datagram replay charges server rate window but
   duplicate cannot advance stored serial; native unique subsequent packet is
   refused at the packet limit and accepted after controlled window advance.
   Expired native relay data is retired without output. No fabricated counters,
   queue entries or ACKs; inherited send wrapper returns controlled success.
   Byte-rate/high-payload cases remain
   distinct required F08 work unless separately established.

Architecture/bounds: retain one shared fixture/global directed voice owner.
Main owns tests/voice_pcm_native_fixture.c (up to550 lines with new finite phases),
builder/docs/results. Luna owns only tests/voice_queue_recovery_native_fixture.h
(up to140 lines), included after existing helpers. This is a tightly scoped test
case family, not a parallel implementation: no bootstrap, queue, parser, codec,
state machine or policy body is copied. The small header isolates edit ownership;
all runtime phases execute sequentially, starting/ending with native empty client
queue and released PTT. Main handles clock/capture and subsequent phase setup.
Reopen if bounds exceeded or ownership/state duplication appears. Source bugs
require a separate narrow repair plan and senior disposition.

Run after implementation/review, retain failed logs, record precise acceptance
limits. Final senior follow-up reviews source evidence/false-pass and lifetime
claims; overall F10 acceptance remains separate. Main/reference and user-owned
migration document stay untouched. All edits/commits remain on2.0.
