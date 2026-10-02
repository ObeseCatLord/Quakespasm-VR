# Native decoded voice to HRTF qualification plan

2026-10-02. Next frozen F08 boundary after f0693c5b native cached-source callback
qualification. Before code, no pass claim. Reuse existing complete assertion-
enabled Steam Audio/SDL3/voice graph and native voice fixture. No production
renderer/decoder/transport/listener/cache/clock rewrite or new fixture builder.
Main inspected Voice_Frame/WriteSpeakerPCM/PublishSpatialVoiceSource,
Spatial_VoiceSource/PCM/ResetVoice, receive-off retirement and shared producer
sequence state. Native voice publishes active only for a named current player
with generation/unmuted status; valid model/msgtime gives positional mouth,
otherwise radio. Actual decoder writes SDK stream when Spatial_Active; ordinary
fallback speaker PCM ring is bypassed.

One optional -voice-hrtf header/function uses source/receiver/native states and
existing SendControlledFrame/RelayToReceiver. Actual source admitted name and
stock player model are prepared acoustic identity/model inputs in receiver
score0/entity1; preserve original name/entity and num_entities. Use public
Spatial_Listener with canonical Quake forward+X/right-Y/up+Z and origin0.
No claim that prepared positions/score came from live movement/publication.

Do not reset the shared Voice_ResetConnection before every synthetic receiver
case: this fixture has one global encoder/sequence owner across clients and
would reset sender sequence while its admitted server connection remains.
Instead native voice_receive0 + Voice_Frame then1 + Voice_Frame retires receiver
jitter/SDK stream/generation without resetting sender sequence. On next real
packet native receive generation setup resets Opus decoder. This is an actual
receive-disable/enable owner, not full reconnect/map-reset proof. Existing caller
key_game/default dummy capture remains; no manual decoder/queue/output fields.

Initialize native sound cvars through S_Init only if snd_hrtf is not registered;
assert -nosound. Actual Spatial_Init creates DSP only, no physical output. Reuse
existing capture-and-forward SA_SetSource observer's borrowed renderer pointer
and public SA_GetStats before shutdown. Current enabled source graph stays
unchanged; header/source integration happens after the prior review is closed.

For each case prepare only receiver entity/model/current-or-stale msgtime and
score/listener; retire old receiver state through native receive toggle first.
Require all SDK voice streams empty before sending. Actual controlled PCM speech
and END encode, native client writer/server parser/relay/full receiver parser
queue new jitter. Step bounded20ms realtime/Voice_Frame, observe actual nonzero
SDK stream_frames before drawing, then actual Spatial_Render float stereo.
Accumulate finite output and independent per-channel energies; consume all jitter
and SDK stream frames. Ordinary speaker0 PCM ring stays empty on this route.
Near right/left current-model cases require directional nonzero signal in both
ears. Far beyond configured distance, missing model and stale msgtime each
require centered sample-wise equal nonzero radio output. Equal samples apply
only isolated fresh receive-reset/room-free radio cases, not arbitrary live mix.
HRTF-off exact-cardinal output already qualified for cached source; do not copy
its zero-ear oracle to voice, which uses narrower pan plus centered radio blend.

Finally create actual decoded speech queued in SDK without rendering all of it;
assert stream_frames>0. Native receive-disable/Voice_Frame retires jitter/
generation/SDK pending stream and renderer remainder. Subsequent bounded callback
blocks are exactly silent in this isolated room-free/music-free/SFX-free context.
Re-enable receive through native owner, restore original receiver name/entity/
num_entities/listener and saved native cvar value, preserve updated client
transport snapshots, restore sender/netcon. Actual Spatial_Shutdown frees DSP,
clear borrowed renderer alias; no input audio caches allocated here. Final
original pending PCM/jitter/held-PTT/client fatal/reset/Host_Shutdown still runs.

Luna/xhigh owns ONLY tests/voice_hrtf_native_fixture.h <=230C, defining guarded
static Voice_HRTFNativeChecks(source,source_state,receiver,receiver_state).
Main owns optional fixture include/call/marker, unchanged builder reuse, private
profile/provenance/docs/review. No existing header/production/builder edits by
worker, not alone/no reverting. Source-only/no nested agents/builds/tests/runtime/
branch operations, atomic>=16MB/fsync/replace/mode0644. Report missing evidence
or cap overflow before widening. Main reviews completed code then one consolidated
CPU-only run with all prior nine-marker options plus this case, fresh preferences,
dummy/no UDP/no sound/no Steam API/dedicated and linked licensed assets only read.
Existing full enabled object graph/library hashes retained, actual inputs stable
before/after execution; selected source provenance is not comprehensive compile-
time attestation. Local Astra reviews actual routing/retirement/oracle/lifetime
and evidence limits after passing receipts; F08/F10 remain open elsewhere.
No GPU/OpenXR/headset/physical recording/output/device failure/performance work.
