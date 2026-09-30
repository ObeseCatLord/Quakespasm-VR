# Inherited diagnostics through existing engine owners

2026-09-30. Restore the useful on-demand movement report and positional sound
probe on2.0. Plan precedes code. No builds, tests, probes or performance
measurements are performed until all implementation is finished. Main/master
and the user's dirty migration-2.0.md remain untouched.

## Behavior and verified references

Primary51b452c0 Quake/sv_main.c:981–1064 registers netdiag and prints client
movement/snapshot counters plus active server peers' movement/snapshot state.
The existing destination net_dgrm.c:1016–1064 net_stats instead reports transport
messages/datagrams/socket sequences. It does not expose client movement ACKs,
permission epochs or the native selected-command queue. Keep that command.

Destination client.h:173–200 already owns movement ACK permissions, coherent
owner associations, command counters, snapshot counters and the current
prediction-error vector. server.h:346–373 owns completed/retired/discarded
command cursors, published authority, input phase and bounded queued duration.
Use these actual native meanings. Do not import primary's obsolete quarantine
latch, renamed counters, new replay policy or another history table merely to
print its old output. On-demand netdiag reports those existing client facts and
each active server peer's protocol, native queue/cursors/epochs, published
authority validity and replacement ACK/transport sequences where available.
Do not fabricate unavailable historical maximum/error/loss counters or call
movement classification/VM callbacks from the report. A dedicated server must
work without a local connected client; disconnected client state is explicitly
labelled. Guard every socket/entity access. No periodic reporting owner/cvar is
needed for this on-demand utility; ordinary scheduling remains native.

Primary snd_spatial.c:55–74 snd_spatial_probe accepts no arguments, a sound
name, or a sound name and three coordinates; requires a signed-on map; defaults
to ambience/fire1.wav128units along the listener's forward vector. It calls
S_PrecacheSound and S_StartSound with an entity identity outside the live range.
Destination snd_dma.c:503 already owns admission/channel/cache/start, and
snd_spatial.c:640–658 treats that ordinary positional channel as SA_POSITIONAL.
Existing play emits at listener_origin and spatial_status emits diagnostics;
neither places a probe at an explicit position. Copy the primary adapter into
the native spatial registration owner, with a useful local name spatial_probe.
Require finite coordinates/default origin, preserve the accepted argument
counts, and let native stopsound/map/channel retirement own cleanup. No direct
SDK source creation, retained probe table, new callback or special mixer path.
The probe remains useful through the native sound fallback; don't require HRTF
or enable/change audio settings implicitly.

## Minimal adapter versus replacement

Add two small console callbacks and their registrations in existing owners:
Quake/sv_main.c and Quake/snd_spatial.c only. Reuse current state, console,
native sound and socket accessors. Replacing net_stats, adding counters to the
hot path or retaining a separate audio source duplicates working owners and is
unnecessary. Estimated combined production scope<=120 net lines; reopen before
new state, another production file or material scope growth.

The primary alicia_spike_stats is separate: r_alicia_spike.c:1–4 and176–192
describe an opt-in USE_ALICIA_SPIKE/-aliciavrm experiment restricted to one
fingerprinted asset and CPU/OpenGL submissions. AV-009 requests preservation of
source/provenance/opt-in status, not promoting that prototype to a general VRM
release renderer. Preserve its pinned reference and experimental distinction;
do not add a second renderer just to copy the command name. General selected
avatars and equipment remain their existing independent migration requirement.

## Implementation and final acceptance

1. Add native on-demand netdiag using existing state and bounded active-peer
   iteration. Label reported facts honestly; guard socket and owner accesses.
2. Copy positional sound placement through native precache/start/register with
   argument/finite validation. No legacy command alias requirement.
3. Main reviews actual output fields/call ownership and scoped whitespace diff;
   update command checkpoint without claiming full feature completion.

After full implementation, Linux/ARM software checks cover disconnected/listen/
dedicated state, active public/private/desktop/VR peers, reset/reconnect/ACK and
queue state, valid and absent sockets, invalid arity/nonfinite probe positions,
default/explicit sounds, native fallback, stopsound and map retirement. Do not
describe source review as microphone/headset or audible spatial qualification.
User live tests and performance measurements remain outside this goal.
