# Native room / monitor senior review brief

2026-10-02. Frozen F08 component; solo project, reuse current native owners.
[Before-code plan](room-monitor-native-final-2.0-plan.md). No production edits.
Main inspected native voice producer/policy, world export, SDK/room ownership,
settings publication and effect/reset/worker teardown. Question: are this minimal
fixture and its acceptance claims sound, without new adapters or production APIs?

| Fact | Evidence and limits |
| --- | --- |
| [verified: source] Native reuse | tests/room_monitor_native_fixture.h is optional in voice_pcm_native_fixture.c. Actual linked Spatial/world/SA/room owners; existing capture-and-forward SA_SetSource observer borrows renderer only. No new wrapper, private room access, synthetic mesh, parser, DSP or loader. Native S_Init under asserted dedicated/nosound if cvars absent. |
| [verified: run] Independent permission | Prepared disconnected client, no session/netcon, desktop TX off. Native setters/forced capture refresh/Voice_Frame open dummy capture for explicit self permission. Actual dummy-produced self queue observed and drained; no outgoing packet/sending state. Controlled actual Voice_EncodeCaptureFrame subsequently admits PCM only to self. Gain1 is a prepared public DSP control after reset, not an assertion about default monitor volume. |
| [verified: run] Wet-only/bound/privacy | With no room, actual two20ms self frames consumed with exact zero output. Third frame exceeds1920 bound and increments drop count. Real e1m1 loaded server sv.models[1] is prepared in cl.worldmodel/listener globals. Actual world exporter creates13911 triangles/positive geometry bytes and real CPU SDK worker, finite-positive RT60/ready/runs. Both mode1parametric and2hybrid consume actual self queue and yield finite positive output. Observed positive partial block plus queued PCM, then native ResetSelf clears queue and16 rendered blocks are exactly silent. Permission-off closes dummy capture and actual producer admits no self PCM/network. |
| [verified: source/run] Room updates | First compile caught incorrect sv.worldmodel field; current fixture uses native sv.models[1]. First runtime omitted settings/listener update after creating room, leaving worker unready. Fixed fixture by actual Spatial_Update after each NewMap, matching native frame publication. Failure receipts retained; no production change or invented results/times. |
| [verified: run/source] Retirement | Own process /proc/self/task comm IDs named room-acoustics captured. Old observed IDs disappear after native replacement; clear and final attached-room shutdown restore original named task set. Native joins/detach/context ordering source-verified. SDK child names may inherit; not exhaustive thread/race/leak proof or callback contention qualification. |
| [verified: run] Consolidated result | /home/obesecatlord/FastGames/qsvr-room-native-_1heinas: build/run/input-stability/completion all0,12 markers, native final client/server/Host shutdown succeeds. Existing11 components retained. Fresh preferences/dummy capture/dedicated/noudp/nosound/nosteamapi; no GPU/OpenXR/window/physical audio/settings. |
| [verified: receipts] Provenance | main-reviewed-summary.json, actual pre/post dependency/link/runtime snapshots377 stable inputs incl221objects/1PCH,28 selected current source hashes. Exe796649d3fecc90bdbcf7558b3a9b285b84a0cfb8b7a5dbdd428507f4bc466c21. Reused enabled native common graph, SDK4.8.1. Snapshots after fixture compile, selected hashes after run; not exhaustive compile-time/loaded-library attestation. |
| [verified: tagged SDK source] IR/geometry ownership | Private sdk-source-provenance.json/tree-selected contain14 official v4.8.1 files/URLs/hashes. GetOutputs exports overlapSaveFIR handle; API effect casts to SDK TripleBuffer; simulation writes/commits, convolution updates read buffer. Native scalar snapshot lock and serialized simulator/GetOutputs, only hybrid consumes IR. StaticMesh/Mesh copy source vertices/triangles/materials. Tagged source contracts, not binary-build identity or runtime race proof. |

Current lean: accept bounded software observations and preserve native room/SDK
buffer ownership; no new IR deep copy, public stats getter or alternate scene
loader. Rejected synthetic cube/private renderer access because actual native BSP
and existing observer suffice. Public settings update is required after attaching
room; do not replace native scheduling to make a fixture pass. Wet-positive,
ring-empty and task-name retirement are intentionally distinct, no complete tail,
acoustic fidelity, task inventory or live callback/physical device guarantee.

Verify-before-critique: inspect current header/integration plus actual receipts
and relevant native owners. Prioritize false passes (room/self isolation, policy
vs prepared controls, partial-block privacy), lifecycle/restoration, SDK handle/
mesh ownership and unnecessary duplication. Merge overlapping issues. If actual
source contradicts the brief, challenge it. Max800 words final prioritized with
file/line evidence; read-only, no edits/build/tests/runtime/branch/commits/nested
agents. No broad185-item/GPU/Windows/ARM review; no human priority decision needed.
F08/F10 remain open beyond this component. Main spot-checks/adopts corrections.

## Correction verification follow-up

2026-10-02 resumed after storage recovered. Earlier review's evidence is retained
under reviewed-before-privacy-correction. Main changed only SA_ResetSelf by clearing
underwater_accum at existing callback-excluded reset boundary (two added lines).
Stronger fixture observes first-half phase from actual SA_Clock delta, queued
self PCM, native-S16 positive first-half/last magnitude>=8 and positive continuation
control. Both explicit reset and public permission-off from another live state
then yield exactly zero converted S16 samples for16 blocks. No intervening reset
before permission-off assertion. All12 combined components pass/current28 sources.
Native SDL3 callback uses clamp[-1,1]*32767 cast to int16_t (snd_sdl3.c); fixture
uses same format boundary only as an output observation, no callback execution.
After native fix raw SDK float permission-off sum2.584e-8/last3.045e-10 persisted;
origin unknown, not claimed numerical-noise cause. No bit-exact float-silence claim.
Prepared alpha.5 tests filter; known dry1 restored. Sanitized RT60/combined hybrid
wet only; name-filtered task membership/source joins, not raw SDK estimates or
unique convolution/race proof. Main retains earlier limits.

Private current main-reviewed-summary.json records377 stable inputs,221objects/
1PCH/28 current selected sources/12 markers and final executable hash. Private
pcm-regression-location.json points to separate old-native object + current fixture
link/run: only old SA owner substituted; old source differs by exactly two added
lines, current source/native graph intact. Old owner exits-6 at post-reset
!room_monitor_pcm_signal assertion; fixed owner combined pass0. No GUI/GPU/device.

Read-only follow-up review <=450words: verify this focused correction, oracle
phase/PCM-boundary control, old-vs-new regression and claim limits. No full185
features/Windows/ARM review or edits/build/runtime/nestedagents. Challenge any
remaining false-pass or unnecessary production rewrite; main owns dispositions.

Follow-up review completed with no blocking code issue. Verified contexts both
gpt-6-astra/xhigh; closed. Reviewer verified exact two-line source/disassembly
change and matching current fixture object in negative/current links. Main
adopted explicit-reset-only negative attribution, converted4096frame/85ms scope,
unknown float residual and no device-buffer/callback/unbounded-tail claims. Final
results/dispositions are in room-monitor-native-current-2.0-results.md.
