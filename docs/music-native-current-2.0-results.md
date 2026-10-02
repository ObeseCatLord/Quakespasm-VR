# Native music EOF / format results

2026-10-02. Frozen F08 music component, no production changes.
[Before-code plan](music-native-final-2.0-plan.md),
[verified senior brief](music-native-final-2.0-review-brief.md).
Local Astra/xhigh review complete with dispositions below; no full F08/F10 acceptance.

The existing enabled native graph/builder and audio fixture retain actual linked
BGM/codec/SDL converter/music ring/Spatial/public renderer owners. Three optional
SDK linkage observers forward every actual read/rewind/close and record results
only. No inline native owner exclusion, alternate decoder/DSP/protocol/state
machine or hardware output. Dedicated/nosound initialization explicitly uses
native codec/BGM init; if needed S_Init registers sound cvars under nosound.

Seven short original40ms440Hz private files exercise five native codec families: U8 mono11025 WAV, S16
mono22050 WAV, S16 stereo48000 WAV, stereo44100 FLAC/Vorbis and stereo48000
MP3/Opus. Actual CPU conversion exits all0; independent ffprobe confirms their
codec/rate/channels. tests/prepare_music_native.py makes a new private directory,
refuses existing paths and records encoder arguments/exits/output hashes. These
signals are generated, not user music/game data; no game/mod asset is changed.

| Native observation | Current result and limit |
| --- | --- |
| Seven nonloop decoder EOF cases | Positive real read bytes, exact format metadata, EOF return0, one native close with corresponding identity, no rewind/read error. Actual SDK queue remains occupied after codec closes, so normal EOF does not immediately clear queued music. No exact track completeness, converter-tail preservation/length or lossy-sample guarantee. |
| Render/drain | Actual public renderer and Spatial_Update pump the native converter and observably consume the SDK ring, reach full8191-frame free capacity and give finite nonzero output. Mono ears agree within1e-6; stereo both channels are positive with dominant-left energy. Two subsequent isolated aligned blocks are silent. No physical device/callback/timing/listening claim. |
| Pause/resume | Native pause before first decode prevents reads and queue admission; two actual blocks are silent. Resume decodes to EOF and gives nonzero output signal through the same native route. This does not promise immediate silence for an already buffered pause. |
| Initial loop / stop / restart | Short U8 stream's actual EOF/rewind succeeds, a positive read afterward is required, and one initial bounded window renders signal. Aligned native stop closes once, restores full ring capacity and produces silent blocks; subsequent format cases restart through native BGM_Play/Stop and converter replacement. No steady live clock/callback cadence is claimed or injected. |
| Ownership | Read/rewind metadata observed while stream alive; integer identity captured before native close, no dereference/retained live pointer after free. Native BGM/codec/Spatial shutdown, alias clearing and subsequent client/server/native Host_Shutdown pass. |

Luna/xhigh source-only worker wrote294 lines solely in the new header. Main
verified effective context/reviewed source, replaced partial registration with
native S_Init and fixed the observer's close-lifetime witness before executing.
Main integrated tiny optional include/call/guard/marker and conditional three
wrapper flags, with no production changes. First combined pass is retained under
first-integrated-run; stronger stereo-both-ear/full-capacity/paused-output oracles
then warranted an affected combined rerun. Senior review added a post-rewind positive-read observation and corrected MP3 source provenance; the final affected combined rerun passes all prior ten checks plus music.

Final private profile (arguments, source/asset/SDK provenance, logs/status):
/home/obesecatlord/FastGames/qsvr-music-native-a30fajph.
Build/run/completion/input-stability all0; eleven markers and native normal exit.
372 dependency/link/runtime inputs including221 objects/1PCH/seven music files
are byte-stable through execution;27 selected source hashes independently match.
Executable SHA256:
3849e9b1cc7519553f5dad8fcb0e727cf2b6893f536bbe754f8bc923a28d02ef.
Common enabled object graph is reused. Manifests begin after fixture compilation;
selected sources recorded after execution, not comprehensive compile-time or
loaded-library attestation. Standalone music-only initialization was inspected,
not separately executed; this run selected all preceding audio options.

Fresh private preferences/dummy capture/dedicated/noudp/nosound/nosteamapi/licensed
pak reads remain. No GPU/OpenXR/window/physical microphone/output or system
settings changed. Prepared signals/virtual DMA/captured core voice transport and
serialized renderer are seams. Wet-only monitor/room worker/device retry/live
clock/callback contention and remaining frozen F08/F10 boundaries stay distinct.

## Senior dispositions

Main verified local gpt-6-astra/xhigh effective settings and closed the reviewer.
It verified the pre-correction pass; original reviewed receipts/runner are retained
under reviewed-before-loop-correction. Main independently checked actual native
EOF flush/pump/rewind continuation, final counter ordering, linked mpg123/tag
objects and native close lifetime. The final affected rerun is main-verified:
four zero statuses, eleven markers,372 stable inputs/221objects/1PCH/27 current
selected sources and the new executable hash above. No production owner changed.

| Finding / recommendation | Main disposition |
| --- | --- |
| P2 ring drain does not independently prove complete EOF converter tail | Adopted reporting limit. Observable nonzero post-closure audio and eventual SDK ring emptiness/silence pass; actual flush/pump is source-verified. Loss of part of the buffered tail could pass. No full duration/completeness or terminal-marker claim. |
| P2 successful rewind can occur without subsequent decode | Adopted code correction. Forwarding observer counts positive native reads after rewind; loop requires that witness and zero rewind failures. Final affected combined rerun passes. No injected clock or decoder state. |
| P2 wrong selected MP3 implementation | Adopted. Actual graph uses snd_mpg123.c and snd_mp3tag.c, not snd_mp3.c/libmad. Original incorrect selected-source receipt is retained; corrected final rerun records both actual owners among27 hashes. Actual mpg123 object was already hashed and the earlier MP3 output pass remains historically valid. |
| Five codec families, seven format cases | Adopted. Stereo positive/asymmetric output does not establish independent-channel fidelity or exact lossy samples. |
| Initialization/lifetime/native reuse valid in combined invocation | Accepted. Dedicated codec/BGM init and guarded native sound registration, integer identity captured before free, renderer alias retirement and native final shutdown are verified. Standalone initialization remains source-reviewed, not executed. |
| Pause/restart/loop scope | Accepted. Pre-decode admission pause, explicit aligned stop and drained format sequences/initial loop window only. Buffered-pause, implicit live replacement and sustained clock cadence remain unqualified. |
| Delete unused rewind result and duplicate final stop | Adopted with the loop correction. Removed unused observer field/assignment and redundant stop before native BGM_Shutdown, preserving actual ownership/cleanup. |

Remaining room wet-only monitoring/worker teardown and host/device/music timing
boundaries stay within frozen F08. This component is not final integration signoff.
