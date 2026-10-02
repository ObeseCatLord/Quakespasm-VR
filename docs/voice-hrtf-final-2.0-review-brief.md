# Decoded voice / HRTF senior review brief

2026-10-02. Frozen F08 component, solo project, no new production architecture.
[Before-code plan](voice-hrtf-final-2.0-plan.md). Main verified actual production
publication/decoder/receive-disable/SDK reset owners before worker delegation.

| Fact | Evidence / qualification |
| --- | --- |
| [verified: source] Native pipeline | voice_pcm_native_fixture.c includes actual voice.c and native server/client parser owners; SendControlledFrame/RelayToReceiver reuse admitted codec/writer/parser/relay/jitter. Captured transport and prepared client states remain seams. |
| [verified: source] Actual SDK route | Voice_WriteSpeakerPCM routes Spatial_Active to Spatial_VoicePCM; actual Voice_Frame/public Spatial_Render and linked SDK owners run. Borrowed capture-and-forward SA_SetSource observer always forwards, no renderer/queue substitute. |
| [verified: source + run] Acoustic cases | Header prepares actual admitted name and stock player model in receiver scoreboard/entity, near right/left current, far, model absent, msgtime stale. HRTF-enabled current-position directional positive both-ear energy (not independently discriminating HRTF); far/absent/stale exact samplewise equal nonzero radio. Not actual live scoreboard/movement publication. |
| [verified: source + run] Fresh output provenance | Native receive disable/enable retires SDK/jitter/generation before each case; sender sequence preserved. Real packet reception resets decoder, decoded SDK stream goes empty to positive, fallback PCM ring stays empty. No output/cursor/queue assignments. |
| [verified: source + run] Privacy | Fresh decoded pending SDK speech, render256 then17 frames with positive signal and remaining stream frames; native receive-disable empties pending SDK/generation, jitter already drained; eight actual callback blocks exactly silent. Isolated no room/music/SFX, serialized direct renderer, no physical device callback. SA_ResetStream discards mixed remainder at this boundary. |
| [verified: source + run] Lifetime | DSP-only Spatial_Init/Shutdown, saved cvars/listener/acoustic identity restored, borrowed renderer alias clears; subsequent existing client fatal/reset/native Host_Shutdown completes. Sound initialization only if cvar absent, nosound assertion. |
| [verified: artifacts] Current run | /home/obesecatlord/FastGames/qsvr-voice-hrtf-834l1r4p: build/run/completion/input-stability0, ten markers. Reuses complete enabled native graph from qsvr-spatial-callback-k5pdo7ld; no common production recompile/new SDK/device. |
| [verified: artifacts] Freshness | 363 actual compiler-dependency/link/runtime inputs,221 linked objects/1PCH stable through execution;17 selected source hashes match now. This is not comprehensive compilation-time source attestation. Executable a05e1b650523b77b9725500d4433793fdd059025bd45b285da509acfa5471d6b. |
| [verified: environment] Safety | Fresh private preferences, SDL dummy capture, dedicated/noudp/nosound/nosteamapi, licensed pak only read. No owned GPU/OpenXR/game window/hardware mic/output/runtime settings. Root space low; all compiler outputs/private logs on FastGames. |

Decision: retain minimal optional fixture header/current builder and native owners;
reject a separate codec/DSP fake, receiver queue assignments, resetting shared
producer sequence between still-admitted cases, or duplicating protocol/state.
Luna/xhigh source-only worker wrote207 lines solely in new header; main integrated
optional include/call/guard/marker. Initial pass retained under first-integrated-run;
main strengthened privacy with actual partial render plus queued remainder and
reran only affected integrated fixture. No production fix.

Verify before critique. Prioritize false-pass/route/privacy/lifetime defects and
claim limits, with file/line evidence. Challenge whether each seam is necessary
and seek deletion/simplification. Check actual final logs/status/provenance, not
just markers. Decide whether bounded component claim is justified and whether
any correction requires an affected rerun. Max900 words final, read-only, no
nested agents/builds/tests/runtime/branch/commits or user-owned docs edits.
Do not re-audit all185 features, previous F08 components, Windows/ARM packaging,
physical callbacks/listening, provider/GPU/performance or whole F08/F10 signoff.
[unknown/deferred] physical device/concurrency/room/music/reconnect/live publication
remain outside this component. No human decision requested.

Post-review clarification: the two reporting corrections above follow the
local Astra findings and main native-source spot-checks; full dispositions are
in voice-hrtf-current-2.0-results.md. No executed input changed after review.
