# Native decoded voice / HRTF results

2026-10-02. Frozen F08 component, no production changes.
[Before-code plan](voice-hrtf-final-2.0-plan.md) and
[verified senior brief](voice-hrtf-final-2.0-review-brief.md).
Local Astra/xhigh review complete with dispositions below; this is not full F08/F10 acceptance.

Actual controlled speech/END traverses admitted native offer/capability, Opus,
client writer/server parser/relay/full client parser, jitter/decoder and the
Steam Audio stream/public Spatial_Render. Existing complete assertion-enabled
SDL3/voice/codecs/CURL/Steam Audio4.8.1 graph and builder are reused; no production
recompile, new decoder/renderer/protocol/framework or hardware device. SDK phonon
and pffft library hashes match the prior enabled callback profile. Only private
child library paths/preferences are used; system libraries/settings stay intact.

| Observation | Evidence and limit |
| --- | --- |
| Five acoustic cases | Prepared receiver score uses actual admitted sender name, entity uses actual stock player model and current/stale timestamps. With HRTF enabled, near right and left produce positive signal in both ears with corresponding dominant energy. Beyond configured range, missing model and stale position each produce nonzero samplewise equal centered radio. Prepared score/poses are inputs, not live network publication proof. |
| Native decoded SDK route | Receive disable/enable retires old jitter/generation/SDK stream before each case without resetting admitted sender sequence. Actual new packet generation resets decoder; native jitter playback makes SDK queued frames positive before rendering while ordinary speaker PCM ring remains empty. No fixture assigns decoded output, queue, SDK PCM, cursor or renderer state. Three real speech/END bursts per acoustic case are drained. |
| Receive-disable privacy | Fresh real decoded speech is left queued; actual renderer primes one256-frame block then renders17 frames, observes nonzero output and queued SDK speech still pending. Native receive-disable clears pending SDK speech/generation and discards mixed remainder; jitter was already empty here. Eight subsequent isolated actual256-frame output blocks are exactly silent. No room/music/SFX; serialized calls do not certify device callback contention. |
| Lifetime and restoration | Actual Spatial_Init/Shutdown with nosound, sound cvars registered only if absent. Score/entity/count/listener/cvars restored, sender sequence advances natively, borrowed renderer alias clears. Existing final pending PCM/jitter/held-PTT fatal disconnect and native client/server/Host_Shutdown still pass. |
| Health | Actual SDK stats report no nonfinite output or instrumented SDK context allocations during callbacks. This is not an allocator-wide or performance measurement. |

Luna/xhigh wrote207 lines solely in the new fixture header, source-only; main
verified effective settings, inspected code, integrated optional include/call/
guard/marker. First integrated pass is retained under first-integrated-run.
Main added a real partial-render privacy witness and repeated the affected
combined fixture once. All prior nine components plus this tenth marker pass;
no common production build, GPU/OpenXR/session/game window or physical microphone/
output is launched. Dedicated/noudp/nosound/nosteamapi/dummy capture are asserted;
fresh private preferences and licensed pak reads are isolated from user settings.

Private actual argv/build/run/status/provenance profile:
/home/obesecatlord/FastGames/qsvr-voice-hrtf-834l1r4p.
Build/run/completion/input-stability all0; ten markers, normal native shutdown.
363 compiler-dependency/link/runtime inputs are byte-stable during execution,
including221 linked objects/1PCH/executable/runner/SDK/headers/pak.17 selected
source hashes match current files. This is not comprehensive compilation-time
source attestation; full enabled common graph was already compiled in the prior
callback qualification. Executable SHA256:
a05e1b650523b77b9725500d4433793fdd059025bd45b285da509acfa5471d6b.

Captured transport/prepared client snapshots/acoustic identities remain seams.
This does not certify fresh live movement/name publication, tracked mouth pose,
receive-toggle callback contention, room wet-only monitoring/worker teardown,
music conversion/EOF/restart, map/reconnect/device read or physical listening.
These finite F08 boundaries and final shipping reconciliation remain distinct.

## Senior dispositions

Main verified local gpt-6-astra/xhigh effective context and closed the reviewer.
It independently checked final statuses, ten markers,363 stable inputs,221objects/
1PCH/17 selected sources and executable hash. Main spot-checked native voice
HRTF-off half-width panning, unconditional SDK binaural call, actual partial-block
remainder and privacy reset, plus the fixture's already-empty jitter precondition.
No blocking route/reset/lifetime defect was established in this scope. Reporting
corrections below change no executed input and require no additional rerun.

| Finding / recommendation | Main disposition |
| --- | --- |
| P2 directional both-ear oracle does not uniquely prove HRTF contribution | Adopted. This is HRTF-enabled native-path smoke coverage. HRTF-off voice also has both ears and directional dominance; disabling HRTF blending could pass these assertions. Actual source/config selects HRTF, and prior cached-source HRTF/panning discrimination remains separately scoped. No independently discriminating voice-HRTF oracle is claimed. |
| P3 jitter is already drained at final privacy boundary | Adopted. Pending SDK speech, generation and partial-render remainder retire observably. Pending-jitter retirement is source-verified here, not exercised with pending jitter at this boundary. Existing independent jitter/reset cases keep their original scope. |
| Minimal header/native builder and forwarded observer | Accepted. No parallel codec/protocol/DSP or new ownership/state machine. Prepared acoustic identity and captured transport remain necessary bounded seams. |
| Isolated serialized eight-block silence/lifetime is bounded | Accepted. No mixed-content, physical callback, race/leak or unlimited-duration guarantee. No production change is required. |
| Recorded-input stability is not complete compile-time or loaded-library attestation | Accepted. Manifest starts after fixture compilation; selected sources recorded after execution. Reused source/SDK provenance remains explicitly qualified. |
| Optional deletion of saved SDK-listener restoration | Not applied. Restoration immediately before DSP shutdown is redundant but harmless local fixture scaffolding. Global fallback voice-listener restoration remains necessary. No production abstraction or additional executed change is warranted solely for this optional cleanup. |

The original brief's broad pending-jitter/HRTF phrasing is corrected to these
limits. F08/F10 remain open for their other frozen software boundaries.
