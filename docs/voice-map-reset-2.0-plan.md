# Voice map-reset adapter

2026-09-30. Before-code plan for the demonstrated AUDIO-004 map/serverinfo
omission. Baseline `93b02dbe`; primary reference
`51b452c018273647dcf94f4628a370267ff8fa91`. Implementation and final Linux/ARM
qualification are pending. No execution checks accompany this plan.

## Behavior and verified evidence

A connected map/serverinfo change must retire the previous voice session's
capture/transmit continuity, speaker PCM/jitter/decoder history and spatial
streams before client state is freed. Per-player mute/gain return to the
inherited new-session defaults; saved desktop/VR microphone preferences and
global cvars remain intact. A disconnected session uses the existing same reset.

The bounded local worker found the call-site omission, not missing jitter or
generation implementations. Main checked the load-bearing claim directly:
primary `cl_main.c:CL_ClearState` calls `Voice_ResetConnection` after clearing
the spatial world and before freeing the client. Current `CL_ClearState`
clears the spatial world, view state and client memory but omits that call.
Current `cl_parse.c:CL_ParseServerInfo` calls `CL_ClearState`, then resets
VRIK and voice transport. `CL_ResetVoiceTransportState` clears negotiation and
the outgoing queue, not the independent speaker state. Current `CL_Disconnect`
already calls both transport reset and `Voice_ResetConnection`.

`voice.c:Voice_ResetConnection` is the reusable owner: it stops transmission,
closes capture, resets capture/session/sequence/talkspurt continuity, clears
speaker jitter, generation, talking state, mute, gain and PCM ring indices,
resets live Opus decoders and clears spatial streams. Speaker mutation uses
the existing audio-buffer exclusion when available. The helper tolerates
uninitialized sound/decoder handles and does not rewrite saved profile settings.
Spatial-world clear and speaker reset take their existing locks separately;
the caller must not hold an extra audio lock around this helper.

## Minimal adapter and scope

Restore one `Voice_ResetConnection()` call immediately after
`SpatialWorld_Clear()` in current `CL_ClearState`. Ownership: only
`Quake/cl_main.c`; expected one added production line. Do not transplant the
primary client clear routine, duplicate decoder/ring reset policy in the
parser, add generation fields or change capability/packet/audio clocks.

Resetting lazily on the next incoming source generation is insufficient:
generation replacement does not clear per-player controls, and a silent source
may send no replacement packet to retire queued old audio. A second reset
implementation would duplicate the established disconnect/capture/decoder
owner. The existing helper is the narrowest adapter.

Commit this plan before code. Delegate the single call-site edit with exclusive
write ownership, then inspect the complete diff and existing callers/helpers.
If the work requires another production module or changes the reset contract,
reopen this plan before expanding. This is a routine inherited lifecycle repair,
not a new audio architecture requiring another design review. The full goal's
final Astra review remains required.

## Final software acceptance

After all implementation, the consolidated Linux/ARM pass must use the actual
serverinfo/client clear path. Seed receive jitter/decoded PCM, generation,
talking, mute/gain and capture/transmit continuity; change maps without a
disconnect and observe their inherited new-session state. Include a source
that sends no new speech, fresh capability/voice resumption, unchanged saved
VR opt-out/default microphone preferences, repeated clear/disconnect and no
initialized audio backend. Check existing spatial-stream and callback exclusion
at their real owners. A helper-only reset fixture does not prove the caller.
Live audible/device trials remain user-deferred.
