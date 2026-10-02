# Native Steam Audio publication/callback results

2026-10-02. Frozen F08 component boundary, no production changes.
[Before-code plan](spatial-callback-final-2.0-plan.md) and
[verified senior brief](spatial-callback-final-2.0-review-brief.md).
Full local Astra/xhigh review is complete with dispositions below; final F08/F10 acceptance is not claimed.

A complete new native Meson target compiles/links all453 jobs with assertions,
SDL3/voice/Opus/codecs/CURL/Steam Audio enabled. All four SDK audio owner objects
are linked consistently. SDK4.8.1 version and borrowed native phonon/pffft hashes
are recorded, with only those two libraries in a private symlink directory;
linker rpath-link and child-only LD_LIBRARY_PATH use it. System SDL3 retained,
no global library/driver/runtime settings changed. Existing fixture builder
gets only conditional SA_SetSource capture-and-forward linkage when actual
compile flags have USE_STEAMAUDIO; no extra excluded object or inline native
owner recompile, new renderer or test framework.

Existing full voice fixture passes -vad -routing -recovery -budgets -fatal-send
-client-send -spatial-fallback -spatial-callback: build/run/completion/input-
stability0, all nine markers and normal Host_Shutdown. Full actual SDK callback
renderer plus all prior voice cases execute in one current process. Fresh XDG
preferences/dummy audio/dedicated/no UDP/no sound/no Steam API, linked licensed
pak0 used only for reading. No GPU/OpenXR/window/hardware microphone/output,
deployed server/user configuration or performance measurement.

| Boundary | Actual observable result / precise limit |
| --- | --- |
| Native initialization | Dedicated host skips sound; explicit S_Init under asserted -nosound registers existing cvars/mutex without output callback. Actual Spatial_Init creates SDK context/default HRTF and native source effects. |
| Cache/publication | Prepared16384-frame mono48k S16 loop0 and one-shot-1 caches traverse actual native SDL conversion/publication. Two nonambient sources at equal gain; observed loop room send0.35 versus one-shot1.0, same gain. This is published room-send data, not actual wet-room output. |
| Partial callback progress | Real Spatial_Render after17,19 and remaining220 frames yields exact consumed public cursors, positive finite output, generation/sample/offset identity. The native256-frame precomputed remainder is drained before pause checks; no zero-latency pause claim. |
| Loop pause/resume | Actual shared pause predicate/publication marks only loop inactive. Its cursor/generation/sample/offset/gain remain, one-shot continues advancing; resume advances loop from retained position. Disabled option advances loop even with cl.paused. |
| Menu policy | Prepared active solo/menu holds loop, game resumes; hosted multiplayer and remote solo/menu keep it advancing. Core policy globals restore before following native network/QC cases. No cursor/result/time assignment. |
| HRTF output | Isolated real positional source/right then left, native snd_hrtf1 and reset, eight callback blocks per case. Independent energies after two warm-up blocks are positive in both ears and favor correct side. Actual SDK binaural processing is linked/called by native renderer. No listening-quality claim. |
| Panning output | Same reset exact-cardinal room-free sources with native snd_hrtf0: total signal/direction correct, far-ear energy exactly0. This specific oracle rejects always-HRTF-on output; zero is not a general off-mode expectation for arbitrary direction/reverb/multiple sources. |
| Health/lifetime | Borrowed forwarded renderer pointer used before teardown for actual SA_GetStats: no nonfinite output or SDK-context allocator calls during these callbacks. No timing benchmark. Native Spatial_Shutdown retires converted samples/renderer before prepared input caches free; observer renderer alias clears and channels/cvars/listener/policy restore. No physical DMA callback exists; direct serialized renderer calls do not establish concurrent/device callback exclusion. |

Luna/xhigh source-only worker wrote260 lines in its sole owned header. Main
verified effective context, inspected complete source and integrated tiny
include/optional call/guard/marker plus conditional builder flag. Main added
remote menu and native health observations and corrected the original panner
both-ear-positive oracle before execution. Narrow initial local Astra advice
confirmed an off-mode false-pass gap: main independently verified 1±pan math,
added exact far-ear0 and reran. Original passing source/receipts remain under
first-integrated-run. Broader native-lifetime/provenance review is separate.

Private profile/actual argv/logs/options/dependencies/SDK/assets:
/home/obesecatlord/FastGames/qsvr-spatial-callback-k5pdo7ld.
Final361 inputs (221 linked objects,1 PCH, executable/runner/SDK/headers/pak0)
are byte-stable before/after execution;16 source hashes independently match.
Executable SHA256:
06c9819fab8329f0d4be603243d72d0f286a3e2c358bd0be63fa0418233c0adf.
A fresh entire enabled object graph was compiled; final shipping package/release
qualification still requires its own exact artifact and integration gates.

Prepared caches/acoustic policy and core voice states/captured transport remain
seams. These results do not certify native SDL device callback or live listener,
voice decoded-to-HRTF, wet-only self monitor, room-worker/room output, music
conversion/EOF/restart, physical microphone/device-read retry, listening quality,
performance or full F08/F10. All prior eight voice markers are requalified on
this enabled graph; earlier disabled-graph receipts remain scoped to their own
historical fixture input. Current ARM package qualification is independent.

## Full senior dispositions

Main verified effective local gpt-6-astra/xhigh context, closed the reviewer and
spot-checked the native consumed-progress publisher, source forwarding, cache
conversion/reset/shutdown order, no-output SDL guard and native HRTF-off math.
The complete review found no P0/P1 and one reporting P2; no production change
or additional executed rerun is warranted by a documentation-only correction.

| Recommendation / limit | Main disposition |
| --- | --- |
| P2 input stability is not complete compilation-source attestation | Adopted.361 executed inputs are stable;16 selected source hashes were recorded after execution and match current files. snd_dma/snd_sdl3 object bytes are included but their source bytes are absent from the selected source manifest. Full enabled build completed, but no comprehensive before/after compilation-source/dependency snapshot is claimed. Final immutable-source shipping reconciliation remains F10. |
| Forwarding observer instead of inline duplicate owner | Accepted. Existing real audio graph/cache/policy/clock/lifecycle remains; the observer always forwards and only records inputs. No architecture change. |
| Cache/renderer/restoration lifetime sound for this invocation | Accepted. Destruction precedes input-cache free; borrowed renderer alias clears, globals restore and native voice/client/server shutdown completes. Individual sample eviction and asynchronous callback contention are not exercised. |
| Partial progress/pause/menu cases sufficient for directed boundary | Accepted. No loop-wrap, one-shot-completion, full S_Update reconciliation or contended publication claim. Published room-send data does not establish wet-room output. |
| Corrected HRTF/panning oracle distinguishes selected output | Accepted. Cardinal off far-ear zero and on both-ear positivity are specific isolated observations, not general acoustic guarantees. Health counters cover instrumented SDK context callbacks, not all allocation activity or timing. |
| Serialized no-output calls are not device callback exclusion proof | Accepted. No physical callback was started; remaining device/room/voice-HRTF/music/live-listener boundaries above stay explicit. |

The first narrow Astra response covered only the off-mode oracle. Its source
correction and affected nine-marker rerun preceded this separate complete
lifecycle/provenance review. Current source/receipts were unchanged during the
full review. This is a bounded component signoff, not final migration signoff.
