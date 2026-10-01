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
