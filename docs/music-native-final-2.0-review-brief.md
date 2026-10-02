# Native music senior review brief

2026-10-02, frozen F08 music component. Solo project; retain native architecture.
[Before-code plan](music-native-final-2.0-plan.md). Main already read actual BGM,
codec/SDL-converter/SDK queue/render/stop owners. No production source changed.

| Fact | Verified evidence / limit |
| --- | --- |
| [verified: source] Native graph | Existing voice_pcm_native_fixture.c helper/complete Steam Audio graph reused. New optional music header calls linked BGM/codec/Spatial owners, no inline owner replacement/new builder/decoder. Three conditional GNU wrap flags only observe and always forward native read/rewind/close. |
| [verified: source] Close lifetime | Main changed worker's dangling pointer comparison to uintptr identity captured before real close; no closed stream dereferenced/retained as live pointer. Stream info copied while valid. Actual close returns before final results. |
| [verified: source] Initialization | Main replaced partial manual cvar registration with native S_Init if snd_hrtf absent under asserted-nosound. Actual S_CodecInit/BGM_Init explicitly initialized because dedicated skips them; BGM/codec/Spatial shutdown afterward, renderer alias cleared. Combined run already has sound cvars from prior components; standalone option init not executed. |
| [verified: assets] Seven formats | Private Python original40ms440Hz WAV U8 mono11025, S16 mono22050, stereo48000 left10000/right3000; CPU ffmpeg FLAC/Vorbis44100 and MP3/Opus48000. tests/prepare_music_native.py refuses existing output directories. asset-generation/status/hash and independent ffprobe format receipts match seven files. No copyrighted new asset/user overwrite. |
| [verified: run] EOF/format/output | Native observers record exact metadata, positive bytes, EOF0, one close and same identity/no rewind/error for each nonloop file. Codec already closed yet actual SDK music ring occupied. Public renderer/Spatial_Update drain converter/ring to full8191capacity, finite/nonzero mono-equal and stereo both-positive/dominant-left output then two silent blocks. Not exact sample/lossy quality/resampler-tail-length proof. |
| [verified: run] Pause/loop/stop/restart | Native pause before decode admits no reads/queue; actual silent blocks then resume EOF/audio. Short U8 loop native EOF/rewind success fills initial bounded window and renders signal. Stop at aligned callback boundary closes once/clears queue/silence; subsequent seven format restarts replace converter through native stop. No long-running device/engine-clock cadence claim; no paintedtime/rawend/output/decoder/queue assignments. |
| [verified: run] Combined evidence | /home/obesecatlord/FastGames/qsvr-music-native-a30fajph final build/run/completion/input-stability all0,11 markers, normal native shutdown. First passing receipts retained separately. Main strengthened stereo both-ear, fullcapacity and paused actual-output oracles before final run. |
| [verified: receipts] Reviewed pre-correction provenance | 372 actual dependency/link/runtime inputs incl221objects/1PCH/sevenasset bytes stable through final run;26 selected source hashes current. Executable15710c7145d89ef4a7539c1b65a828c4412bbda6f179c260342cc073e5635fb7. Reused complete enabled common graph; manifests start after fixture build, source selection after run, no comprehensive compile-time or loaded-library attestation. |
| [verified: environment] Safety | Dedicated/noudp/nosound/nosteamapi/dummy capture/private preferences/licensed pak reads. No physical output/GPU/OpenXR/window/settings changes. Build/encode/logs private FastGames, root low. |

Decision: keep native BGM/codec/converter/queue owners and optional minimal observer
rather than duplicate codecs/DSP or inject decoder/stream/output state. Preserve
native clear behavior: existing mixed partial block can remain on explicit stop,
so checks align to block boundary, not demand a rewrite. Observer/captured core
voice/client states/virtualDMA/asset signal and serialized renderer are seams.

Verify-before-critique: read current header/integration/link flags and actual final
receipts; prioritize EOF false-pass, loop bounds, initialization/identity lifetime,
source policy/state duplication and claim limits. Max800 words final with lines,
no editing/builds/tests/runtime/branch/commits/nested agents. Do not re-audit ten
previous audio components/all185 features/Windows/ARM/GPU/headset/provider/perf.
Room wet-only monitor/device retry/concurrency/live clock and fullF08/F10 remain
open. Main handles dispositions/corrections; no human decision is needed.

Post-review: preserve the reviewed run under reviewed-before-loop-correction.
Main applied the recommended positive-read-after-rewind witness and removed two
redundancies, corrected actual mpg123/tag source selection, then ran the affected
combined fixture. Final27 selected sources/new executable and bounded EOF wording
are in music-native-current-2.0-results.md with full dispositions. This brief's
26-source/15710c71 hash denotes the earlier reviewed artifact, not the final rerun.
