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
