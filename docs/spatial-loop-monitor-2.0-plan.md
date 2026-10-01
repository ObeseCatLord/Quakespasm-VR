# C15/C16/C17 native audio publication correction plan

2026-10-01. Before-code plan against the final senior-reviewed checklist.
Reference: native vkQuake loop pause behavior and inherited primary51b452c0
loop room-send/local wet-monitor defaults. No audio runs or executable checks.

## Behavior and verified current owners

Native snd_mix.c pauses cached loops when snd_pauselooping is enabled and either
cl.paused or an active single-player server has key_dest != key_game. Spatial
publication in snd_spatial.c currently marks every cached source active. The
SteamAudio callback checks generation before inactivity; inactivity preserves
playback position but clears filter/tail state. Thus pause must change activity
only, preserving sample identity, generation and offset. No callback rewrite.

Primary snd_spatial.c applies room_send *= .35f for loops after the existing
ambient/UI exclusion. Current publication omits this factor. Primary voice.c
initializes voice_self_reverb_volume to 0.6; current initializes 1. Saved values
and the separate self-monitor permission remain authoritative.

## Adapter versus replacement

Extract the native predicate into one pure S_ShouldPauseLoopingSounds helper in
snd_dma.c, declared in q_sound.h. Native S_PaintChannels and Spatial_Update call
that helper. This prevents policy duplication and adds no audio clock, queue,
pause state machine or playback owner. Evaluate once per spatial update and mark
only cached looping samples inactive. Existing one-shots/music/voice owners stay
independent. Apply the loop send factor and change the wet-monitor initializer.

Exact write set: Quake/snd_dma.c, Quake/q_sound.h, Quake/snd_mix.c,
Quake/snd_spatial.c and Quake/voice.c. One Luna xhigh coding worker; main reviews
and integrates. Networking/renderer work is disjoint. Remove the mixer's now
unused local extern if appropriate; no unrelated cleanup or formatting.

## Stages and eventual acceptance

1. Share the exact current predicate through the existing sound interface.
2. Project its result into source->active only for entry->sample.loop >= 0;
   preserve all sample-change generation/offset logic and stopped-source handling.
3. Multiply loop room_send by .35f at the existing assignment; leave gain and
   ambient/UI admission unchanged. Change only the fresh wet-monitor initializer.

Final software acceptance must show looping playback cursor continuity across
pause/resume and solo menu entry/exit, option disabled behavior, remote multiplayer
menu behavior, normal one-shots and unrelated music/voice continuity. Verify loop
wet send and unchanged dry gain, fresh/saved wet-monitor settings, independent
permission and default-system microphone/VR saved opt-out policy. Source review
does not prove callback timing or audible output. No tests/builds/probes/fixtures
or game execution until all implementation is complete.

The final Astra scope review adopted these native corrections. Reopen if the
shared predicate needs a new thread/policy owner or callback/playback redesign.
