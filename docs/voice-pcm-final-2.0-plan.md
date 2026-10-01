# F08 controlled PCM and negotiated voice: before-code plan

2026-10-01. Source exists in native voice.c, cl_main/cl_input/cl_parse and
sv_main/sv_user; these own Opus/VAD, queued admission, serialization/relay,
jitter/decode and PCM mixing. Missing qualification is not an incompatibility
that justifies replacing any owner. Retain inherited voice behavior and the
existing source-port adapter. Use the existing mixed_native_fixture bootstrap,
stock QC/client states and captured unreliable transport, with a test-only
include of voice.c to reach its capture-frame producer. No production API,
parallel queue/session/codec, actual microphone recording or listening gate.

Compare routes: a new live-network/audio runner would duplicate process and
profile orchestration before the codec/PCM boundaries are qualified. The minimal
incremental route adds one <=400-line native fixture using existing fixtures and
engine objects, then separately qualifies real socket/audio-device behavior.
Captured sends cannot prove connected delivery or runtime timing. Synthetic PCM,
prepared native client signon/resources and controlled DMA are explicit seams.
No assigned voice protocol/capability/packet/generation or decoded-audio values.
Native command/parser consumers must derive those. Dummy SDL recording only,
with SDL_AUDIODRIVER=dummy required before audio initialization and the actual
selected driver asserted dummy immediately afterward; never host devices.

## Reference and build boundary

Main read the unchanged quakespasm-openvr/Quake/voice.c capture producer
(lines423–482) and PCM writer/mixer owners as the inherited behavioral reference.
The migration keeps their VAD/preroll/Opus-to-PCM flow with native vkQuake audio
and profile/transport adapters. No donor body is reimplemented in this fixture.
Current preliminary DEBUG Meson host build has voice/Opus and SDL3 enabled,
Steam Audio disabled; spatial/HRTF cannot be certified by this configuration.
Use its exact recorded voice compiler and current221-object link graph, excluding
the five owners included by the fixture (main_sdl,sv_main,cl_demo,cl_parse,voice),
and existing Loop_Init/NET_CanSendMessage/NET_SendUnreliableMessage/player-skin
wrappers. Preserve strict compiler flags; no warning suppression or stubbed
Opus/jitter/voice queue. Private build adapter/argv receipts remain with logs.

## Finite slice

1. Initialize existing dedicated stock engine fixture and two spawned peers.
   Retain native protocol offers/voice capability. Extract the actual publisher's
   voice offer from its retained serverinfo bytes and run the native client
   parser/capability producer and server command consumer. Prepare resources
   using CreateMixedPeerState, not a replacement signon implementation.
2. Initialize actual voice owner in isolated preferences, controlled DMA and
   SDL dummy audio. Feed deterministic 20ms PCM into Voice_EncodeCaptureFrame
   with native profile/PTT/VAD gating. Use actual Opus, client queue/CL_SendMove,
   server SV_ReadClientMessage, SV_SendPendingVoice and full CL_ParseServerMessage
   callback; captured unreliable transport is the only network seam.
3. Advance controlled time through actual Voice_Frame jitter/Opus playback and
   Voice_MixAudio. Require nonzero output and depleted real ring; no synthetic
   decoded samples or fake jitter/Opus. Check mute silence/gain and missing/reordered
   captured packets at the existing transport boundary if within the bound.
4. Observe native reset retirement and newer/older server generation handling,
   clean Voice_Shutdown and original engine shutdown. This slice does not close
   whole F08: live transport, full capture routing/defaults/retry/PTT/VAD matrix,
   Steam Audio/HRTF/music/spatial worker and map/slot scenarios remain distinct.

Ownership: Luna xhigh implements tests/voice_pcm_native_fixture.c ONLY, no builds
or game runs; main owns plan/build/integration/README/results. Reuse source and
existing helpers rather than copying their bodies. Report any actual blocker,
source incompatibility or bound overrun instead of adding framework/stubs or
production changes. Main reviews complete diff and records actual results.
Stop/reopen if the fixture duplicates owners or exceeds400 lines. Main/reference,
user migration doc and installed assets remain untouched.

The reusable mixed fixture exposes MIXED_NATIVE_FIXTURE_ENTRY; rename that
entry for inclusion, as the existing local-load/co-op fixtures do. Keep the
original negotiation fixture entry renaming unchanged. No bootstrap copy.

## Main integration review before execution

Luna xhigh delivered204lines and stopped at main handoff. Main identified
fixture-only assumptions: initial two native command sends are suppressed,
first admitted voice serial is1, six playout ticks can buffer multiple PCM
blocks, cls.message retained a stack capability buffer, and preferences were
hardcoded to a shared tmp path. Reuse native command warmup, require advancing
serial relative to its real baseline, mix exactly the observed buffered frames,
retain stable capability storage and require caller-isolated dummy/preferences
environment. Use Voice_RefreshCapture(true), the existing routing owner, after
preparing the test preference. No source/queue/codec replacements or relaxed
production admission are justified. Also require a positive completion marker.

The first native run fails before audio: SpawnPeer's stock spawn command retires
the earlier serverinfo buffer, so extracting its voice offer afterward sees
none. Preserve the offer at the existing held reliable-send boundary instead:
the existing NET_CanSendMessage wrapper copies the native published offer while
it is still in serverinfo, then refuses transport exactly as before. The normal
client parser and server capability consumer still derive all negotiated state.
No fake offer literal producer, copied spawn bootstrap or assigned capabilities.

The retained-offer run reaches actual Opus/native relay and its first parser
callback inserts a valid packet (generation1,sequence0,talkspurt1,payload55).
The two isolated test bursts did not deliver the producer's PTT-release END
marker; playout legitimately advances beyond the sparse second packet. Before
retrying, feed another controlled capture frame after actual PTT release and
deliver its native END through CL_SendMove(NULL)/server receiver/relay. This
models a completed burst using the existing producer, rather than resetting
jitter or fabricating sequences/talkspurts to make a sparse fixture pass.

Both complete audio bursts, native mute and voice reset/shutdown now pass,
then overall offline engine shutdown reaches NET_Close's null driver callback:
the reused fixture creates qsocket descriptors while intentionally disabling
Loop_Init. Before retrying, retire those fixture-owned synthetic descriptors
through existing NET_FreeQSocket before invoking native host shutdown. Preserve
native voice/engine shutdown; do not add a production/network-close shim or
assign a mock driver callback. This cannot certify connected socket teardown,
which belongs to the separate live network acceptance.

Endpoint retirement avoids the null driver but native host shutdown next finds
the component bootstrap still owns an active QC VM. Reuse the normal native
client-drop consumer while that VM is active after freeing its driverless test
descriptor; then release the VM through PR_SwitchQCVM(NULL) before native host
shutdown. This retires synthetic fixture peers before transport-wide broadcast,
removing disconnected-socket polling noise. No fabricated driver/send result.
All prior failed native logs remain diagnostic, not passing evidence.

Native SV_DropClient retires active/netconnection; it leaves old capability
fields until normal slot initialization. The relay's existing active guard
prevents retired sources. Check native inactive/null-netconnection, not an
invented immediate voice_capable-zero policy. No product change is required.

The checked-in default Makefile does not configure voice objects/defines, so
the generic negotiation make recipe cannot reproduce this voice-enabled slice
unchanged. Retain the primary Meson compiler/object graph. Before adding the
reproducible command, permit one <=75-line voice-specific builder that reads
compile_commands.json and ninja's existing native link command, replaces only
the five fixture-included owners, and adds the existing four wrappers. No new
compiler policy/configuration or general fixture framework. Preserve exact
flags, library order and actual Opus dependency; main owns this helper.
