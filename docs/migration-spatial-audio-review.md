# Steam Audio migration: senior design disposition

Local Astra `gpt-6-astra` at `xhigh` reviewed the vkQuake sound owner against
the inherited Steam Audio implementation. The effective model and effort were
checked in local session metadata. This is an architecture decision, not an
audio parity or platform qualification claim.

| Review recommendation | Disposition |
|---|---|
| Prefer the donor's late-pose callback renderer, but preserve vkQuake's channels, sound loading, music decoding and SDL owner. | **Adopt, narrowed.** Port `snd_spatial.c`/`snd_steamaudio.c` as an optional projection/render path at the existing sound boundary. Do not copy the old sound engine. The alternative of adding HRTF inside `S_PaintChannels` is technically viable, but its default 100 ms mix-ahead bakes head orientation too early for inherited VR behavior. |
| Replace donor's 1,024-entry, `sfx_t *`-keyed PCM table. | **Adopt.** vkQuake holds up to 4,096 descriptors and moves half of them during recycling (`snd_dma.c:S_FlushOldestSounds`); pointer identity is unsafe. Attach immutable spatial payload/generation to its cache or an equivalently stable owner, and quiesce callback references before reclamation. |
| Keep one active playback clock and preserve legacy sound behavior. | **Adopt.** Specify 48 kHz versus DMA sample units and one cursor authority per backend. Preserve looping/pause, underwater filtering, ambience/menu sounds, independent static emitters and music EOF/backpressure. Backend selection occurs at sound initialization/restart; failures use the legacy renderer. |
| Fix donor room teardown before porting simulation. | **Adopt.** Donor `Spatial_ClearWorld` calls `SA_LoadRoom` under the audio lock, while `SAR_Destroy` joins the simulation thread. Detach/publish under brief callback exclusion and destroy/create outside it. Stage room work after direct HRTF; keep its separate result handoff. |
| Route Opus through one voice lifecycle. | **Adopt.** Branch mono decoded PCM before current stereo panning, never feed both playback queues, and reset queue/remainder/effect tails on mute, receive disable and source generation replacement. Local wet-only reflection extends existing microphone consent policy; it does not create another capture owner. |
| Do not equate software fallback with three-platform feature parity. | **Adopt.** Missing linked `libphonon` can prevent process startup; packaging must provide matching native libraries. The inherited prebuilt SDK lacks Linux ARM64. Build/qualify its pinned source natively for Steam Frame or record spatial audio as an unfulfilled release target. The user's three-platform target keeps this as implementation work, not an optional delivery downgrade. |

The first vertical proof is one stationary mono world source with late listener
rotation and front/back/elevation response, while desktop and `-sndlegacy`
retain ordinary playback. Then add voice, muzzle, occlusion/reverb, and local
wet-only reflections at the same owners. Software checks should include sample
table rollover, reused source slots, independent static loops, music EOF,
voice resets, map teardown and device restart. Hardware listening is reserved
for the user's later testing.

## Current source-owner checkpoint (2026-09-30)

The historical interface index uses primary settings such as
`voice_radio_filter` and `snd_reverb_mode`. Their literal absence in2.0 is not a
missing DSP feature: current `snd_spatial.c:76–84,107–115,499–507` registers and
projects `snd_spatial_room_*`, `snd_spatial_radio_*` and
`snd_spatial_voice_reverb` through the existing settings snapshot. The user
does not require old setting-name compatibility. Keep these owners and do not
port another audio service from the index alone.

Current `snd_steamaudio.c:99–119,445–462` contains the inherited radio filter,
compression/drive and voice-reverb send. `snd_room.c:63–161,164–202` retains
copied scene/simulation worker and result publication; `snd_spatial_world.c`
adapts native BSP geometry rather than replacing its loader. Room replacement
and clear (`snd_spatial.c:920–957`) detach under brief callback exclusion and
join afterward, as the adopted architecture requires. This is bounded source
evidence for AUDIO-008/009 ownership and reuse, not audible or concurrency
execution qualification. Those final Linux/ARM checks remain deferred.

## Receive jitter and voice-session source checkpoint (2026-09-30)

The bounded local source comparison found `voice_jitter.[ch]` unchanged from
primary `51b452c018273647dcf94f4628a370267ff8fa91`: a 16-packet bound, adaptive
60–140 ms playout target, 20 ms deadlines, wrap-aware ordering, duplicate/late
rejection, PLC and 400 ms stale reset. These are copied mechanisms, not a claim
of measured latency or audible quality.

Current `sv_main.c:SV_VoiceReceive` admits validated negotiated packets through
existing rate/sequence budgets; relay includes slot/generation.
`cl_parse.c:CL_ParseVoicePacket` consumes the full bounded payload, gates
capability/source/generation/packet validity and calls the registered receive
callback. `voice.c:Voice_ReceivePacket` feeds the inherited jitter queue, resets
PCM/Opus/talking/spatial state on a new generation and rejects retired generation
packets. Deadline-driven `Voice_Frame` decoding and existing native/spatial
playback consume that state. Commands resolve name/slot, retain clamped gain
and mute, and the audio consumers apply them through their existing owners.
Gain/mute are inherited slot controls; generation replacement alone does not
reset them. This checkpoint does not claim a new person-identity mute policy.

Main confirmed one omitted lifecycle call: serverinfo used `CL_ClearState`
without the primary `Voice_ResetConnection`, while disconnect already used it.
The [before-code map-reset plan](voice-map-reset-2.0-plan.md) led to the one-line
repair `c5482976`; it clears old capture/transmit, speaker/PCM/jitter and spatial
continuity through the existing helper before client teardown. Map/session
reset restores inherited per-player defaults and preserves saved microphone
profiles. The full diff and call/helper boundaries were source reviewed.

Final Linux/ARM qualification still needs actual transport-to-jitter-to-playback
coverage, loss/reorder/wrap and END/stale handling, mute/gain changes, slot and
generation replacement, real serverinfo map clearing and capture resumption.
No tests, builds, compiler checks or runtime probes were performed in this pass.

The companion [optional capability retry](optional-capability-retry-2.0-plan.md)
repairs consumed voice offers when the native reliable buffer is full in
`b5bd74ea`. Pending voice intent stays separate from active receive/transmit
admission; the existing client send loop retries the complete reply independently
of VRIK. Existing reset owners retire it. Main and the final requested-Astra
bounded source review found no P1/P2 blocker. Actual negotiation, mixed-peer
budgets and capture/playback qualification remain deferred.

## Actual optional voice transport source reconciliation (2026-09-30)

Main inspected the actual capture-to-queue-to-client/server/recipient chain for
AUDIO-001/NET-027, independently of the receive-jitter checkpoint:

| Existing owner | Source-supported behavior |
| --- | --- |
| `voice_protocol.h` / primary definitions | Retains version1, 48kHz mono, 20ms/960-sample frames, 400-byte maximum payload, exact header fields and START/END/RADIO flags. Existing queue/budget constants consolidate primary client/server definitions; this is not byte-identical header reuse. Primary copyright/license notice is restored without changing definitions. |
| `snd_dma.c:241,916` / `voice.c:Voice_Init,Voice_Frame,Voice_QueuePacket` | Native sound initialization/frame owns optional Opus capture/encoding, callback registration and playback. The 24kbps VBR/DTX encoder produces bounded packets through `CL_QueueVoicePacket`; stopping transmit clears queued speech and can enqueue END. Meson's Opus selection includes this existing owner. |
| `cl_main.c:CL_QueueVoicePacket` | Requires live signed-on negotiated transport, rejects invalid packets, holds four copied packets and replaces oldest speech when capture outruns blocked networking. Map/disconnect transport reset clears admission and the queue. Pending capability intent from b5bd74ea is not active transport. |
| `cl_input.c:CL_AppendVoicePacket,CL_ConsumeSentVoicePacket` | Public and private movement paths append voice only after their movement/ACK/optional pose work. Requires complete remaining capacity, no overflow and the512-byte client budget. One queued packet is consumed only after native unreliable send returns1; blocked sending retains it. Initial private command suppression retains the existing optional-pose/voice branch. |
| `sv_user.c:SV_HandleVoiceCapability,SV_ReadVoicePacket` | Exact negotiated capability establishes receive/relay eligibility and skips pre-opt-in history. Reads the entire bounded payload even when admission fails or datagram acceptance limit is exceeded. Malformed/truncated framing follows native message errors; valid over-limit packets are consumed without queueing. |
| `sv_main.c:SV_ReceiveVoicePacket,SV_SendPendingVoice` | Retains primary duplicate/rate guards (75packets/16KB per second),32-packet source queue, nonzero generation and0.25-second stale age. A separate at-most900-byte native unreliable datagram follows ordinary snapshot/VRIK submission, additionally bounded by peer MSS. Existing round-robin source order and three-per-source tick limit remain. Live relay cursors commit on send result1; stale queued packets may retire without sending. |
| `cl_parse.c:CL_ParseVoicePacket` / `voice.c:Voice_ReceivePacket` | Parser consumes complete framing, bounds source/generation/capability/payload and dispatches the synchronous registered callback. Existing recipient owner rejects retired generations, resets prior PCM/Opus/spatial state and copies the new payload into inherited jitter storage. Native/spatial audio consumers remain unchanged. |
| Signon and server-map owner | Offers are appended only when they fit after ordinary serverinfo/precaches in the same message. No separate loading-time offer or reduction of mandatory precaches. Server-map reset retires capability/queue/cursors and advances active source generations. |

This records source integration and reuse rather than network latency,
crossplay, audible quality or successful platform delivery. It does not change
the server offer-capacity policy or introduce a second audio/network owner.
Final Linux/ARM checks must exercise actual negotiation, public/private send,
blocked send and gameplay-first saturation, stale/rate/duplicate handling,
ordinary incapable peers and transport-to-jitter-to-playback continuity. No
build, test, compiler check, fixture, game or benchmark ran in this checkpoint.

## Audio HUD source reconciliation (2026-09-30)

Main traced the actual AUDIO-005 update and drawing consumers against the primary
HUD/voice source. Host_Frame calls native S_Update once after rendering;
snd_dma.c:916 calls Voice_Frame before native/spatial sound dispatch. Capture/VAD
publishes atomic meter/transmit state, and jitter decode/end/stale/disable updates
atomic speaker state. Voice_HUDEnabled/InputLevel/CaptureReady/IsTransmitting/
SpeakerTalking only read those snapshots. No per-eye capture/encode/update.

Sbar_Draw calls Sbar_DrawVoiceStatus before its CSQC/native branch. The existing
GUI owner uses SCR_DrawVRHUDPanel with prepared native modern/classic transforms;
the shared voice drawing selects panel-local or desktop canvas, preserves the
previous canvas, bounds the speaker list to MAX_SCOREBOARD16, and distinguishes
OFF/LIVE/READY/NO DEV. The primary draws the same basic status/meter/name list;
2.0 adapts its native canvas and adds capture-failure visibility.

This is source ownership/reuse, not readable-stereo or audio/UI qualification.
The [capture continuity brief](voice-capture-continuity-2.0-plan.md) records a
separate demonstrated producer gap: discarded/error data can leave old LIVE and
preroll state. Its repair47df36e6 reuses existing voice reset/capture owners,
with final source-review receipt in that plan. Broader
Linux/ARM audio qualification remains at the end of implementation; no builds,
tests or runtime checks ran in this reconciliation.

## Spatial inventory reconciliation (2026-09-30)

Main rechecked AUDIO-008/009 actual producers and consumers before updating
their inventory rows. Spatial_ApplyCvars projects current room/radio/occlusion/
voice-reverb controls into the existing settings snapshot. Spatial_UpdateOcclusion
uses a bounded world-only trace budget with source-generation/world/motion/age
invalidation; render_block smooths obstruction and radio mixing while applying
the inherited filter/compression/drive. This establishes the existing generic
owners, without old setting-name aliases or an additional DSP implementation.

The room worker publishes reflection results under its existing lock and
coalesces input at a maximum10Hz without a growing work queue. SAR_Render and
render_block use try-lock snapshots instead of waiting for game-thread work.
Spatial_ReplaceRoom/ClearWorld detach under native callback exclusion, release
it, then destroy/join the detached room. These source contracts are present;
their timing, result ownership under SDK execution, map-teardown concurrency,
audible transitions and native ARM library delivery still require the final
consolidated Linux/ARM qualification. No implementation rewrite or execution
check was introduced by this inventory reconciliation.

## Local wet-only capture source reconciliation (2026-09-30)

Main compared primary voice.c/snd_steamaudio.c with the actual AUDIO-010
producer/consumer path. Voice_RefreshCapture combines independent profile
self-reverb permission with available recording device and Spatial_Active,
without requiring a network session or enabling network transmission.
Voice_EncodeCaptureFrame sends PCM to Spatial_SelfPCM before its separately
gated multiplayer/transmit/PTT-or-VAD path. Voice_Frame publishes the local gain
through Spatial_SelfGain. Menu/profile controls and explicit console confirmation
keep local permission independent; the selected system-default microphone and
saved VR transmit opt-out policy remain their existing owners.

SA_WriteSelf bounds local queued PCM to two20ms frames. render_block consumes
that ring only into room_send; it never adds dry self PCM to mixed/network voice.
SAR_Render supplies the room effect afterward. Disabled gain discards pending
self PCM, and Spatial_ResetSelf excludes the callback before clearing the ring,
gains, remainder and room effect state. Capture continuity repair47df36e6 uses
that same reset. These copied/adapted source contracts reconcile the old
AUDIO-010 label without another recording device or monitoring implementation.
Actual audible wet-only output, independent capture/transmission, mode/profile
resets and Linux/ARM package execution remain final qualification; no tests,
builds, microphone capture, audio or runtime probes ran in this checkpoint.
