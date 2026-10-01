# F08 directed native voice recovery results

2026-10-01. Incremental extension of the accepted
[native PCM component](voice-pcm-current-2.0-results.md), using the same
preliminary Linux DEBUG Meson engine through aa828629, actual SDL3/Opus and
existing captured-delivery wrappers. No production edits. Parent481 lines and
separate140-line queue-case header reuse all native owners. Main reviewed the
complete Luna header; routing was verified gpt-6-luna/xhigh before handoff.

Completed implementation compiles0 and runs0 with both
VOICE_RECOVERY_NATIVE_PASSED and VOICE_PCM_NATIVE_PASSED, then native shutdown.
Only dummy recording and isolated preferences are used. Source/builder retain
their assertion-enabled input and actual-driver guards. The focused senior
review and affected rerun are recorded below; this is not full F08 or F10 closure.

| Boundary | Observation |
| --- | --- |
| Client queue pressure | Native PTT/capture-frame producer fills capacity+3 without sends. Count stays bounded and all retained native sequences are the freshest contiguous capacity. Native stop replaces this pressure with END; native serialization/server admission retires it. |
| Packet-rate limit | A captured native encoded datagram is replayed until75 accepted charges in the current rate window. Duplicate serial never advances. Actual unique END is refused at the limit, then admitted after controlled next-window time. No assigned counters/sequences/packets. |
| Relay expiry | Native sender advances the receiver cursor over actually stored packets older than the native age limit and emits no datagram. |
| Gain | Actual numeric-slot voice_player_volume0/1; decoder output is nonzero in both cases, mixed output is silent at zero and nonzero at one. Native mixer consumes each observed ring. |
| Reorder / one omitted frame | Three contiguous native encoded frames and END are independently relayed, captured and delivered2,0,1,END or2,0,END. Native jitter sorts the received stream; actual playback yields exactly three frame blocks and consumes END. Starting with an empty ring, the omitted middle frame itself contains nonzero native concealed PCM before mixing; the mixer consumes all three blocks. This is controlled-input recovery, not perceptual quality. |
| Reused source slot | Old decoded PCM and pending jitter exist before native source drop/spawn/resource setup/offer negotiation. New source generation is monotonic; actual new stream clears old PCM and replaces jitter. Replaying actual old relay bytes leaves the new generation, jitter and empty PCM unchanged. New stream then decodes/mixes. |
| Lifetime | Retired source resources free only after global client aliases point to its newly created state. Current clients remain alive through native disconnect/Host_Shutdown; final aliases clear before free. |

The earlier run exits134 because the generic relay capture helper required a
payload beyond the16-byte header. The newly isolated actual END datagram is
valid with exactly that length. Native debugger shows captured_sends1 and
captured_length16 in the loss helper. Main changes only the helper lower bound
to include a header-only datagram; actual full parser and jitter/end assertions
remain. The failed run and diagnosis remain separately retained.

No connected socket cadence or independent live client is certified: two native
client states use one process/global voice owner, synthetic PCM/DMA, prepared
core client protocol/signon/resources, desktop PTT preference and controlled
realtime. Inherited transport wrapper returns controlled successful sends; this
slice cannot establish actual socket acceptance or send failure. The END retry deliberately
redelivers actual captured bytes after the window; it is not a production
reliable retransmission guarantee. Source-slot generation replacement differs
from receiver reconnect or map reset. Byte-rate/high-payload/server-pressure/
send-failure, live delivery, device/default/saved profiles, complete VAD/preroll/
discontinuity, HRTF/spatial/music remain distinct frozen F08 obligations.

Evidence root /tmp/qsvr-final-qualification-thchgzi8:
voice-recovery-current/reviewed/{voice-pcm,compile-argv.json,link-argv.json};
logs/voice-recovery-current-reviewed-{build,run}.log, both exit0.
The earlier end-marker-fix build/run also pass but have the weaker aggregate
concealment signal assertion; the reviewed result supersedes that observation.
Initial build/run and diagnosis logs are distinct and superseded.
Licensed pak0 remains a read-only link in a private basedir. Main/reference,
installed assets, user preferences and deployed servers are untouched.
[Before-code plan](voice-recovery-final-2.0-plan.md),
[verified senior brief](voice-recovery-final-2.0-review-brief.md).

## Focused senior dispositions

Main verified both effective review contexts gpt-6-astra/xhigh; the reviewer
could not inspect those settings itself. Read-only source/retained-evidence
review, not whole-goal F10 signoff.

| Recommendation | Disposition / independently checked result |
| --- | --- |
| Aggregate output does not prove nonzero concealed middle audio | Adopted. Main read native NULL-payload Opus path and added empty-ring plus circular middle-block/both-channel signal check. Reviewed build/run0 establishes this controlled input; no quality claim. |
| Core protocol is prepared; native send success is controlled | Adopted. Main checked inherited CreateMixedPeerState core fields and wrapper return1; brief/plan/receipt now distinguish these seams from native voice negotiation and codec delivery. |
| Retain existing native owners and finite lifecycle scope | Adopted. No production changes or new owner; new-generation arrival replaces old audio, not immediate departure clearing. Independent receiver/map, live/send-failure/byte-budget and other F08 obligations remain open. |
| Lifetime repair remains sufficient for reviewed paths | Accepted with limits. Main checked global aliases change before retired-source frees, and surviving resources free only after Host_Shutdown. No hardware/live-client certification. |
