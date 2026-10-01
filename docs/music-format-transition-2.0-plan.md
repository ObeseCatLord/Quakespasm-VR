# Music format transitions at the native stop boundary

2026-09-30. Before-code plan for AUDIO-011; software qualification remains
deferred until implementation of the full surviving migration scope finishes.

## Behavior and source evidence

Keep native vkQuake codec discovery, track selection, pause/resume, looping,
volume defaults and legacy mixing. Spatial playback must accept the next track
after an explicit stop/track change even when its sample rate, width or channel
count differs. Natural EOF retains queued audio and the resampler tail.

Current `bgmusic.c:BGM_Play/BGM_PlayCDtrack` call `BGM_Stop`, which calls
`Spatial_ClearMusic`. That clears the callback music ring under existing DMA
exclusion and clears the SDL converter, but retains its format metadata.
`Spatial_MusicSpace` then returns zero on a different format, before decoding;
`Spatial_RawSamples` also rejects changing a live converter. This prevents
the next native stream from progressing. The primary OpenVR implementation
recreated its converter on format changes; the migrated bounded adapter added
the guard and must retire format ownership at the deliberate stop boundary.

The converter is used by host-thread `Spatial_PumpMusic`, `Spatial_RawSamples`
and lifecycle functions. The callback consumes the SA music ring, not the SDL
converter. `Spatial_FreeStream` already abstracts SDL2/SDL3 and accepts NULL.
Native `snd_codec.c` is unchanged against the pinned vkQuake checkout. Audio
device setup keeps the adapter at 48 kHz and native mixing at `snd_mixspeed`;
shutdown already frees and nulls the converter and metadata.

## Adapter decision and ownership

Smallest repair: in `snd_spatial.c:Spatial_ClearMusic`, after the existing
ring-clear exclusion, free the converter, set it to NULL and reset all three
format fields. Keep sample-clock and diagnostic resets. Reuse the existing
SDL abstraction. Retain same-stream mismatch rejection in MusicSpace/RawSamples
and the EOF drain path in FinishMusic. No new state machine, format-switch
policy, decoder, queue or playback clock is needed. Recreating implicitly on
every mismatch could discard an active track's queued samples and would weaken
the bounded ownership contract; replacing the music engine has no demonstrated
need. Expected production write set is this one function, a few lines.

## Acceptance and limitations

At the final Linux/ARM pass, exercise actual native track selection with
different rates, mono/stereo and 8/16-bit source samples, including explicit
stop, track replacement after natural EOF, repeated stops, same-format
replacement and backend/device restart. Verify finite bounded queue progression,
no stale converter output after explicit stop, native looping/pause behavior,
and natural EOF drain. Cover SDL2/SDL3 selections where supported and native
fallback without Steam Audio. The existing renderer may retain one already
mixed callback block after music clear; do not reset unrelated voices/world
sources merely to remove that inherited bounded remainder.

Before those deferred checks, main and requested-Astra source review inspect
the actual patch and callers. Source inspection does not prove audible output,
dependency packaging or performance. Headset listening stays with the user.
