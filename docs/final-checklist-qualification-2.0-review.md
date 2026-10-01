# Final exhaustive checklist: local senior disposition

2026-10-01. Local `gpt-6-astra`, explicit `xhigh`; effective model/effort
independently checked before and after review. Input:
[verified brief](final-checklist-qualification-refresh-2.0-brief.md).
Read-only source/evidence review, with no reviewer edits/builds/tests/SSH/children.
Production snapshot48e026e0; later fixture/document integrations are identified
below. Main/master and user-owned migration-2.0.md remain untouched.

## Exhaustive source conclusion

**No additional missing source feature was established. C02's reviewed source
obligations are present. Required defect repairs, verification and delivery
remain unfinished.** Source presence is not executable acceptance.

Inventory and worksheet contain185 unique matching IDs, with no missing/extra
IDs or duplicates. **Exact unreviewed IDs: none.** Astra reconciled current
receipts with the prior detailed source audits and independently checked C02
and challenged qualification findings. Unchanged implementations retain their
prior reviews; this is not a fresh line-by-line or executable certification.

Coverage: BASE-001–003; VR-001–016; WPN-001–012; COOP-001–013;
MOVE-001–012; AV-001–009; FBT-001–005; MOD-001–014; UI-001–006;
AUDIO-001–011; XR-001–012; PLAT-001–008; PERF-F001–003; NET-001–029;
PERF-001–023; ASSET-001–009. The
[185-row crosswalk](final-scope-enumeration-2.0-worksheet.csv) remains historical:
153 S,18 M,two Q,11 X,one R. Every M row maps to C01–C22 source receipts;
both Q rows map to integrated Q01. Thus173 rows have source/native dispositions,
11 excluded dispositions and one research disposition. These are scope
classifications, not accepted-feature counts or completion percentages.

Supplemental QC/command/history/preservation inventories retain their native,
retired and research dispositions in
[the senior scope record](final-scope-senior-disposition-2.0.md) and
[interface/history record](final-scope-interface-history-2.0.md). Historical
names alone do not establish omissions. Eleven useful-addition candidates
remain optional research, not approved implementation.

Astra checked actual server projection/envelope owners at sv_main.c:5317/5584,
dirty obligations and signon phases, post-QC retirement at host.c:613, client
reply/demo/protocol capacity at cl_main.c:1612, and refused-seta flags at
cvar.c:156. Main previously read the complete C02 patch and checked current
pext registration. The [C02 source receipt](metadata-publication-final-integration-2.0.md)
is supported; actual pressure/parser/QC/lifecycle acceptance remains pending.

## Confirmed repair obligations

- Desktop Vulkan WRITE_AFTER_WRITE hazards and ordinary-quit allocator abort.
  Independently built untouched vkQuake reproduces both symptoms. Identical
  root cause and driver fault are unproved. Keep clean validation/normal exit
  required and preserve native pass/teardown owners.
- Required GCC13 -O3 warnings-as-errors builds fail: effective_view warning in
  stereo setup on amd64/arm64; cached format-result warning on amd64.
  Astra checked R_StereoSceneView at gl_rmain.c:804: failure leaves output
  untouched, success copies/scales the current eye. Caller already checks a
  frame and supplies valid eye/storage; reachable repeated-lookup failure is
  unproved. GL_DensityOffsetSceneFormatSupported at gl_vidsdl.c:1597 assigns
  result on cache hit/miss under stable-count assumptions. These are build
  blockers, not demonstrated runtime defects or new missing features. Plan
  narrow explicit guard/initialization fixes without warning suppression.
- Broad controller fixture fails link due to incomplete current stubs: a fixture
  defect, not an established product defect. Repair it or provide equivalent
  actual native-input evidence.

## Complete remaining work and main dispositions

All eight groups remain open. Detailed actual-owner cases in
[the qualification plan](final-linux-arm-qualification-2.0-plan.md) remain
binding; this table groups rather than deletes them.

| Senior recommendation | Main disposition / remaining acceptance |
| --- | --- |
| 1. Complete native builds | Adopted. Repair portable compile blockers; finish Linux x86-64 client/dedicated and native ARM64 client from one immutable revision, with OpenXR, shaders, codecs, CURL and Steam Audio. Preliminary host build disables Steam Audio; both third portable attempts failed at engine compilation after dependencies built. |
| 2. Native desktop behavior and defects | Adopted. Identify exact hazardous attachment/pass scopes and localize shutdown corruption before bounded corrections. Qualify campaigns/mission packs, controls, graphics/AO, native demos, console/config order, transitions and normal shutdown. Baseline failure does not waive acceptance or authorize a renderer rewrite. |
| 3. Stereo/runtime/input/foveation/avatars | Adopted. Credit clean24-probe actual simulated-XR rendering, but complete either-eye culling, wet/dry transparency, materials/particles/liquids/precision/lightmaps/effects/diagnostics, all HUD/menu/wheel/intermission/mirror/mask paths, body/equipment/muzzle/shadow/TLAS lifetimes; session/focus/reference/recovery; profiles/handedness/rearm/haptics/tracker identity/filter/staleness/roles; eye opt-out/freshness/full-quality restoration, FB/META preference/startup KHR fallback, explicit-only fixed mode and protected detail. CPU helpers do not prove GPU foveation. |
| 4. Networking/metadata/weapons/audio | Adopted. Review native C02 worker evidence; preserve all pressure/fit/privacy/overlay/signon/slot/map/fastload/refusal/demo cases. Complete public/private desktop/VR snapshots, prediction/ACK/loss/reorder/cadence/smoothing/discontinuities/capability/CSQC events, IPv4/IPv6/discovery and install/switch/reconnect. Qualify shared solo/MP calibration, paired ranged inputs and gesture-only melee through QC. Complete Opus framing/jitter/budgets/generations, system-mic/default-on-opt-out/PTT/meters, HRTF/fallback/radio/occlusion/reverb/loop/wet-monitor and music transitions. |
| 5. Loaded QC/co-op/saves/discovery/UI | Adopted. SSQC/CSQC permissions/signatures/results/errors; simultaneous-VM resources/reload/abort; strings/files/buffers/search/reflection/surfaces/entities/drawing/input/localization. Preserve entity/round-query adapters, physics/liquid/customphysics handoffs, co-op inventory/callback/collision/respawn/late join/QBJ3 limbo, autosaves/v5-v7 saves/hubs/reference liveness, resource reuse and Unicode/search/catalogue/config/default precedence. Registry counts are insufficient. |
| 6. Assets/maps/performance mechanisms | Adopted. Model/image/fullbright/WAD3 precedence, dynamic precaches/path fallback, liquid lightmaps/interpolation, BSP/malformed references, worker/serial preparation, visibility/batching/allocations and cleanup. Retain stock/AD/ad_tears/q30a1024/QBJ3/Enyo/Dwell/Mjolnir mj4m1, tershib/shib1_drake, peril/tavistock, jumbo BSP2/no-VIS and original-level z-fighting cases. Missing assets mean missing evidence. Load/render/leave without heapsize workarounds; no timing/speedup gate. |
| 7. Relocated artifacts/upstream maintenance | Adopted for both architectures' resources/defaults, ABI/GLIBC<=2.39, dependencies/aliases/RUNPATHs, both loader contexts, notices/original receipts/source access and negatives. Rehearsal conflict enumeration/documentation is complete:14 files/35 blocks. Adapted maintenance requirement: record accepted adapter/deferral dispositions and unresolved coupled risks; require merged build/behavior evidence before claiming a resolved rehearsal. The clone is unresolved. This does not require every newer upstream feature or a production merge. |
| 8. Evidence integration/final signoff | Adopted with freshness correction. Six reviewed fixture adaptations already integratedaacc54e9; current stale no-tests/builds statements refreshedab295b73. Those tasks are closed. Broad input/native C02 proof remain open. Reconcile exact revision/requirement evidence, fix failures, rerun affected coverage and obtain final local Astra integration review. Eighteen helper/dispatch fixtures are partial evidence, not18 accepted features. |

## Deferred/excluded scope and limits

Windows builds and user live headset/gaze/provider/listening/multiplayer/
performance trials remain deferred. Actual hardware testing is outside goal
completion; software limits must be documented instead of claiming live support
from fake-runtime results. AV-009 remains preserved research.

Preserve exclusions: quad views, skyrooms, Gorilla/hand-swim propulsion, instant
stop, contact melee/parry/hybrids, revival, Mjolnir dual-state weapons, imagedump,
VR/additional demos, legacy setting/MP-offset aliases and general incompatible-
device-loss reconstruction. Ordinary desktop, swimming/ladders, gesture melee,
one solo/MP calibration and surviving native graphics remain required.

No additional adopted-scope omission or human decision was established. This
freezes required feature scope; later actual defects attach to these owners.
No final-goal signoff: software/delivery obligations remain open.
