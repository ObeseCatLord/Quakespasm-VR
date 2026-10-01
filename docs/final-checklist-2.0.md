# Final 2.0 migration checklist

**Reviewed final scope checklist. The migration itself remains incomplete.**
2026-10-01. Original audited production snapshot: `2b420380`; refreshed against
source integrations through `99eea1f0` and paused uncommitted C07/C14 patches.
Both enumeration reviews changed documentation only; intervening source commits
are listed below. No final-tree builds or tests have run. The user-owned changes
to migration-2.0.md remain untouched.

Following that review, C07's draw phase was source-integrated in `1b5b2d84` and
C14's optional extraction phase in `19e2c6b3`, producer in `3b80ac1a` and native
consumers in `4179ef1e`. **Two implementation areas now remain: C02 and C19.**
None of these
source receipts constitutes final executable acceptance.

Local Astra xhigh reviewed all 185 inventory rows: 153 source-integrated/native
(S), 18 missing (M), two unresolved (Q, one shared rendering question), 11
excluded/deferred (X), one reference experiment (R). These overlapping rows are
not completion percentages. The missing rows produce the 22 distinct items
below; no row remains unreviewed.

The [current local Astra xhigh refresh](final-checklist-refresh-2.0-review.md)
accounts for all185 IDs using the original review and current receipts, verifies
challenged consumers, and confirmed **four remaining implementation areas at
that review snapshot**. C07 and C14 have since been source-integrated, leaving two.
It found necessary C07/C14 refinements, not a fifth independent missing feature.
Source integration and software acceptance remain separate.

The checklist preserves vkQuake's engine, renderer, resource owners and desktop
baseline. Each missing behavior below belongs at an existing boundary, using
the pinned primary/QSS-M reference where applicable. It is not a request for
another renderer, VM, networking stack or movement solver.

## Final remaining implementation checklist

**C02 / NET-021 — server and player metadata publication**

Receiver argc, nonnegative bounded scoreboard slots and terminated full
replacement are source-integrated in `2a43c96b`, following the
[receiver plan](metadata-receivers-2.0-plan.md). Complete-command quote/trailing
validation and the explicit full-userinfo reader allowance are integrated in
`c2f8d0fd`, following the [command plan](metadata-command-validation-2.0-plan.md).
Native enumeration and old/committed-value scratch buffers now match the existing
8192-byte stores in `44592006`, following the
[local-capacity plan](metadata-local-capacity-2.0-plan.md). This removes the local
1023-byte prefix limitation; it does not certify sender framing or peer capacity.
Publication/privacy/envelope work remains; these partial receipts do not close C02.

- [ ] Integrate explicit directional reader-capacity declaration at native pext,
  then implement complete recipient-envelope checks and visible permanent-oversize
  handling. The [local Astra reopening](metadata-capability-reopen-2.0-review.md),
  `c42cc32d`, resolves reader selection technically without an answer to the
  optional stock-peer question. The [capability slice](metadata-capability-2.0-plan.md)
  is in progress; unknown peers retain token1023/text2046 limits. Arbitrary stock
  large-field parity is not claimed; compatible bundle policy remains to be specified.
- [ ] Publish initial/empty serverinfo before signon2 and all current userinfo
  slots before signon3 using current stores and native reliable-buffer retry.
- [ ] Preserve publication across mid-signon changes, map changes and spawn/
  fastload buffer clears; deliver retired/empty or reused-current slot metadata.
- [ ] Retire only after disconnect QC sees old state; keep current native name/
  colors after custom snapshots and retain native frags.
- [ ] Share full/incremental public projection including the cvar path, exclude
  private underscore keys, retain star keys and clear unrepresentable old values
  without silent1024-byte truncation or changes to local stores.
- [x] Repair full/update receiver argc, bounded nonnegative slots and full-string
  termination; `2a43c96b` (source integration, final qualification pending).
- [x] Finish quoted-command validation and extend only the explicit required
  full-userinfo reader allowance; `c2f8d0fd`. Main source review checked native
  span parsing, distinct closing quotes including lone-opening refusal,
  unchanged quoted LF, trailing-whitespace admission and existing truncation
  checks/generic command limits. Final software qualification remains pending.

**C07 / NET-014 — source-integrated; final qualification pending**

- [x] Source-review and finish the renderer phase; committed native
  replay/ACK/camera phase alone does not close C07.
- [x] Apply one captured frame offset to local desktop and VR held-weapon draw
  matrices, retaining native bob/FOV, identity, dispatch and winding. The paused
  wrapper's desktop omission was corrected in `1b5b2d84`.
- [x] Correct applicable mode2 beam near endpoints after authoritative tracing,
  including controller and paired rays; impacts/gameplay origins remain native.
  The reviewed controller omission was corrected in `1b5b2d84`.
- [x] Keep non-controller HUD offset and camera-derived controller HUD coherent
  without double correction; use consistent foreground/diagnostic draw consumers.
- [x] Preserve default-off finite bounded smoothing, exact command/ACK history,
  semantic resets, and unchanged gameplay/collision/transmitted tracking/muzzles.

**C14 / AV-006 — source-integrated; final qualification pending**

- [x] Reopen/source-review the extraction phase (216 changed lines versus
  210-line threshold), fix the unfinished helper rename and finish optional pair
  extraction through existing model-owned slots/upload/free owners; `19e2c6b3`.
- [x] Separate verified source key/digest/geometry admission from selected target
  ATTACH_HAND policy; `19e2c6b3` removed that erroneous source-policy gate.
  Frame preparation's selected-target branch is integrated in `3b80ac1a`.
- [x] Stage source identity/readiness and publish two immutable attachment records
  at the existing frame owner: dominant-hand shotgun and upper-spine wrench,
  correct source presentation/socket mapping, skin/glow and independent bounds;
  `3b80ac1a` after main source review (398 changed lines).
  The [phase2 senior reopening](qbj3-equipment-phase2-reopen-2.0-review.md)
  corrections are integrated: relative cache indexing, effective-target staging
  gate and native affine reuse.
- [x] Extend native raster, co-op overlays, ShowTris and matching TLAS count/
  emission to both records, with all masks before outlines and consistent shadows.
  The [consumer senior reopening](qbj3-equipment-consumers-reopen-2.0-review.md)
  corrections are integrated in `4179ef1e`: native per-record helper reuse,
  rolling skin fallback alignment, duplicate declaration removal and simpler
  temporary state (225 changed lines, combined840 within855 revised bound).
- [x] Keep the selected body on optional pair failure, publish neither equipment
  nor derived muzzle, avoid Ranger substitution and preserve ordinary Ranger plus
  independent C13 death/corpse presentation and lifetime rules.
  Main source-reviewed complete-count/matrix/shade/inflation/AS preflight and
  conditional draw/emission. Rendered/software acceptance remains required.

**C19 / PLAT-003 — portable Linux x86-64 and native ARM64 release artifacts**

- [x] Select exact SDK/tool/dependency pins and flags, portable ISA, OpenXR
  JsonCpp/system configuration-prefix choices and explicit host/bundled SONAME
  policy; bounded before-code slices committed in `4b485cd6` after the senior
  disposition. [Inputs](portable-linux-2.0-inputs.md) and
  [plan](portable-linux-2.0-plan.md); build qualification remains pending.
- [x] Implement shared native builder using Meson installation and reused
  Steam Audio4.8.1 recipe with required SDL3, shaders, codecs and CURL enabled;
  `8059edb7`, including main source-derived compressed-input/SDK-prefix repairs.
  No execution/build qualification yet.
- [ ] Stage complete dependency closure, explicitly seed executable-side OpenXR
  loader, normalize each ELF RUNPATH and preserve internal SONAME aliases;
  reject unresolved/conflicting/escaping dependencies and retain GLIBC<=2.39.
  The oversized763-line draft remains unaccepted. The
  [local Astra staging disposition](portable-linux-staging-reopen-2.0-review.md)
  and updated [staging plan](portable-linux-staging-2.0-plan.md) are committed in
  `0eb04c2a`. The returned refinement also exceeded its bound (973+41 lines) and
  remains paused/unaccepted. The [second reopening](portable-linux-staging-second-reopen-2.0-brief.md),
  `9c3a1570`, records main-verified version/ownership/notice/duplication defects
  for local Astra review before further refinement. No artifact acceptance yet.
- [ ] Include matching dependency notices, header/static contributions,
  versions/hashes/patches and source-access artifacts through existing owners.
- [ ] Implement artifact verification at the shared staging owner.
- [x] Implement isolated source-in/results-out Foundry transport for one immutable
  snapshot, without modifying the deployed server; `2785feb9`. Tar/checksum
  retrieval preserves aliases; depends on the unfinished staging/verifier slice.
  Execute builders/wrapper/verifiers only in final qualification after implementation.

C02's reader-selection contract is now a reviewed technical decision; the optional
stock-peer question is not a prerequisite to its capability slice. Complete sender
policy and C19's staging consolidation still require their specified implementation.
The detailed plans and review evidence below define their native integration seams.

## Original audit findings and source integrations

The table below preserves the audit's 22 implementation findings. Twenty items
were subsequently source-integrated in the commits below, following committed
before-code plans and main review of the Luna patches. Their executable
acceptance remains pending. **Two implementation items remain**: C02 and C19.
C07's native replay/camera phase is integrated in `3f8b398c` and its
renderer/HUD phase in `1b5b2d84`. C14's three phases are integrated in
`19e2c6b3` / `3b80ac1a` / `4179ef1e`. The shared Q01 rendering correction is also source-integrated;
its rendered acceptance remains pending.
No tests or builds have run since the audit.
Each remaining slice needs a committed before-code plan, implementation/source
review, then observable acceptance in the single end-of-implementation Linux/ARM
qualification phase.

| Source-integrated since the audit | Plan / remaining acceptance |
| --- | --- |
| C07 — optional native ACK presentation smoothing | [Smoothing plan](prediction-smoothing-2.0-plan.md); commits `3f8b398c` / `1b5b2d84`. Main checked native gameplay matrices retained, desktop bob/FOV and pointer/winding preserved, controller/paired beam near endpoints translated after trace with world impacts unchanged, controller HUD offset inherited once and non-controller target explicitly corrected. All final replay/rendered acceptance remains pending. |
| C14 — inherited QBJ3 shotgun/back-wrench attachments | [Equipment plan](qbj3-equipment-2.0-plan.md); commits `19e2c6b3` / `3b80ac1a` / `4179ef1e`. Main checked model-owned optional extraction, source/target policy separation, copied staging context and immutable records, relative cache identity, native affine and conservative bound union, complete-pair raster/overlay/ShowTris/TLAS preflight, rolling skin fallback, zero-pose identity views and selected-body/no-muzzle optional miss. Ordinary Ranger/C13 source paths retained; all final installed-content/rendered/lifetime acceptance pending. |
| C08/C09 — Toss support validity and elevator relink | [Physics plan](toss-support-elevator-2.0-plan.md); source commit `0d2c182c`. Final physics/content qualification pending. |
| C22 — bounded classic-particle capacity and 32-bit quad indices | [Particle plan](classic-particle-capacity-2.0-plan.md); source commit `0d2c182c`. Final dense-particle rendered acceptance pending. |
| C01/C03/C05 — late-model helper, ordinary QC metadata lookup, truthful protocol offers | [Networking plan](precache-infokey-protocol-2.0-plan.md); source commit `ab1423fb`. Final remote precache/QC/negotiation qualification pending. C02 publication is still separate. |
| C15/C16/C17 — shared loop pause, reduced loop room send, inherited wet-monitor default | [Audio plan](spatial-loop-monitor-2.0-plan.md); source commit `ab1423fb`. Callback code/sample identity/generation/offset and independent mic permissions retained. Final software audio qualification pending. |
| C04/C06 — loading nop and missing-model cancellation | [Loading/reconnect plan](loading-keepalive-reconnect-2.0-plan.md); source commit `486d1b42`. Main checked independent nop buffer preserves parser/reliable state and current true-to-abort cleanup. Final peer/loading/reconnect qualification pending. |
| C10/C11 — sampled avatar root yaw and viewer eligibility | [Avatar plan](avatar-root-viewer-gate-2.0-plan.md); source commit `73bd354b`. Main checked immutable published yaw, preparation-time muzzle override and shared viewer/sender eligibility. Ordinary selected-avatar animation remains available without tracking. Final body/prop/muzzle/shadow and toggle acceptance pending. |
| Q01 — per-eye alpha categories at liquid boundaries | [Stereo transparency plan](stereo-water-transparency-2.0-plan.md); source commit `a7d06c01`. Main reviewed the Luna six-file adapter, serialized exceptional recording, context resets and local non-alias angles. Shared sorting, native passes and opaque single-pass stereo remain. Final rendered boundary/OIT/context acceptance pending. |
| C12 — inherited QBJ3 live-player admission | [Live-avatar plan](qbj3-live-avatar-2.0-plan.md); source commit `33d6b90a`. Main reviewed exact model/frame/live admission, explicit choice precedence, unresolved-descriptor fallback and implicit tracking recheck; raster and BLAS share eligibility. Final installed-content/rendered acceptance pending. |
| C13 — independent QBJ3 selected death/corpse presentation | [Corpse-avatar plan](qbj3-corpse-avatar-2.0-plan.md); source commit `f96ad216`. Main reviewed the unified bounded frame owner, reserve-before-fill/player priority, current selection/colormap identity checks, explicit death animation and no native live-tracking fallback, independent palettes and raster/BLAS consumers. Final installed-content/lifetime/rendered acceptance pending. |
| C20 — physical native VR developer field table | [Field-panel plan](vr-field-panel-2.0-plan.md); source commit `a42de70f`. Main checked actual default-canvas ortho/viewport inversion, frozen HUD pose, finite/dimension gates before division, paired panel restoration and unchanged collector/desktop content. Final two-eye/resolution/context visual acceptance pending. |
| C18 — inherited combined-build/component notices | [Notice installation plan](license-install-2.0-plan.md); source commits `fb0f41d9` / `3adf25fb`. Copied primary GPL3 text and unmodified official Valve4.8.1 notices through Meson/Nix source/install owners, including vendored Monado ABI provenance. Source-copy hashes checked; installed/artifact notice qualification pending. C19 remains separate. |
| C21 — valid GPU samples and shared stereo diagnostics | [Diagnostic plan](gpu-diagnostic-validity-2.0-plan.md); source commit `d659894c`. Main reviewed per-slot sampled modes, successful-query/conversion validity and measured-zero distinction; existing queries retained. Final software timing/status/mode-change acceptance pending. |

| Item | Feature IDs | Original audit gap and smallest implementation seam | Source evidence / eventual observable acceptance |
| --- | --- | --- | --- |
| C01 | NET-007 | Fix late QC model precaches: call the existing correct SV_Precache_Model helper instead of duplicating its logic with a sound tag. | pr_cmds.c:1497/1540; cl_parse.c:2977/3002. QSS-M pr_cmds.c:1206/1239. A late model reaches the model cache and renders, without corrupting sound slots. |
| C02 | NET-021 | Publish full initial serverinfo/userinfo and clear retired-slot custom metadata through existing signon/spawn/drop message owners. | sv_main.c:3578; host_cmd.c:2423; host.c:646. QSS-M sv_main.c:2276/host_cmd.c:9802/host.c:1437. Joining and slot reuse yield current custom keys, with no stale occupant data. |
| C03 | NET-021 | Restore ordinary SSQC infokey world/player fallback using existing Info_GetKey stores. | pr_ext.c:4478/4528 versus QSS-M4622/4676. Special keys remain native; ordinary keys and absent values agree with published metadata. |
| C04 | NET-007 | Restore client loading keepalives at main-thread precache boundaries; preserve packet/parser and reliable-message state. | cl_parse.c:1908/2281/2290; primary641/864/871. Slow model/sound loading maintains a remote connection without losing queued gameplay/control data. Do not add an I/O thread. |
| C05 | NET-020 | Remove unsupported DP7/BJP3 offer tokens from the existing challenge handler. | net_dgrm.c:2672 versus cl_parse.c:2060. Negotiation offers only NQ/Fitz/RMQ decoder support. New dialect implementations remain optional. |
| C06 | NET-022, UI-004 | Cancel automatic reconnect on missing required models and return through existing loading/menu cleanup. | cl_parse.c:2286; cl_main.c:665–673; primary cl_parse.c:850–862. Missing/stale installed mod fails once, no timed retry loop. Current parser aborts with true: do not copy primary's false literally. |
| C07 | NET-014 | Restore optional bounded reconciliation presentation smoothing on existing command/ACK identities and view preparation; keep discontinuity resets. | Current cl_main.c:275 resets but2059 publishes directly; primary1595/1618/1683/1949 and view.c:836 implement producer/decay/consumer. Enabled smoothing reduces bounded presentation correction; disabled/teleport paths remain immediate. No new quarantine/protocol/authority. |
| C08 | MOD-011 | Validate Toss support after Think, clearing invalid/freed/nonsolid non-world ground before the grounded early return. | sv_phys.c:11745–11749 versus primary6767–6804. An object falls after QC removes its support; world support remains valid. Existing robust pusher records are not this check. |
| C09 | MOD-011 | Relink a successful legacy elevator Z nudge with SV_LinkEdict(check,false). | sv_phys.c:3279–3284; primary4414/QSS-M922–930. Subsequent collision/visibility sees final position without duplicating trigger callbacks. |
| C10 | AV-002 | Apply accepted interpolated body yaw to the tracked render root shared by body, props, muzzle and shadow consumers. | r_alias.c:687/1028; primary5961–5975. Pose/entity update differences keep all rendered tracked outputs facing the sampled body frame; retain immutable local-pitch repair. |
| C11 | AV-002 | Restore viewer-side vr_vrik enable and inherited supported-game eligibility at preparation. | r_vrik_render.c:826/1101; primary r_alias.c:5809/5961 and vr.c:3229. Disabling tracked posing preserves ordinary selected-avatar animation; existing Enyo restriction remains. |
| C12 | AV-006 | Restore inherited QBJ3 live-player admission and explicit-avatar precedence using the supported player_qbj.mdl/143-frame contract. | r_vrik_render.c:472/476; primary r_alias.c:5735/5772/5840. Eligible tracked QBJ3 players use installed rig; unresolved/mismatched assets retain original mod art. Adapt raster and BLAS eligibility together. |
| C13 | AV-003, AV-006 | Preserve selected QBJ3 death/corpse presentation without stale live tracking; enumerate attributable queued corpses through current scoreboard-colormap identity. | r_vrik_render.c:465/1250; primary r_alias.c:5749/5836. Explicit resolved alternates survive supported deaths/corpses; native default art remains otherwise. Each corpse owns its palette, not the living owner's. |
| C14 | AV-006 | Restore QBJ3 shotgun/upper-spine wrench equipment and its optional-equipment fallback using native model-owned props/attachments. | r_vrik_render.c:709/743 and gl_model.c:6957 versus primary r_alias.c:5262/5280/5570. Skin/glow/bounds/shadows agree; missing equipment leaves selected body visible without substitute Ranger props. |
| C15 | AUDIO-011 | Project the native looping-pause predicate into spatial source activity while preserving sample, generation and playback cursor. | snd_mix.c:444/469 versus snd_spatial.c:674 and snd_steamaudio.c:396. Loops pause/resume consistently for actual pause and solo menus; voice/music policies remain independent. |
| C16 | AUDIO-009 | Restore primary's 0.35 looping-source room-send multiplier at the existing source assignment. | snd_spatial.c:679 versus primary359. Loop wet send is reduced; dry gain, UI/ambient admission and room worker remain unchanged. |
| C17 | AUDIO-010 | Restore inherited local wet-monitor default 0.6 while retaining saved values and independent permission. | voice.c:83/1429 versus primary52. Fresh settings retain inherited wet level; opt-out/mic selection/remote routing stay independent. |
| C18 | PLAT-004, AUDIO-006 | Copy the inherited GPL3 combined-build notice and applicable component/third-party notices through the existing build source closure/install/package owner. | flake.nix25/49 currently carries only native LICENSE.txt; inherited SPATIAL_AUDIO.md:50/release workflow104 carry LICENSE-GPL-3.0.txt. Required notices accompany SteamAudio-enabled artifacts. |
| C19 | PLAT-003 | Finish a portable Linux x86-64/ARM64 artifact route at the existing Meson/install/dependency owner, retaining GLIBC 2.39 and clean executable-side loader discovery. | flake.nix and docs/linux-native-builds.md currently define native store builds, not portable distributions. Reuse reference packaging where applicable; clean install starts with native architecture dependencies and no local build/store path assumption. Windows build remains deferred. |
| C20 | MOD-014 | Restore inherited physical VR developer field-panel placement at the existing DrawInfoPanel canvas/transform boundary. | gl_screen.c:1136/2447 and Shaders/basic.vert:36. Collection exists at gl_rmain.c:1884. Enabled diagnostics are readable in the tracked stereo UI; desktop collection/presentation remains native. |
| C21 | PERF-F001 | Reconcile native frame/AO GPU diagnostic presentation: label shared stereo work and distinguish unavailable/invalid samples from a measured zero. | gl_vidsdl.c:4456/4509/4523; gl_rmain.c:2784. Retain native queries/counters, no invented per-eye times or duplicate profiler. Disabled/failed/wrapped queries never masquerade as valid samples. |
| C22 | MOD-010 | Restore bounded classic-particle capacity and use 32-bit indices in the existing static quad index buffer. | r_part.c:163/204/215/238/1021; primary26/30. Validate parameter presence/count before conversion/allocation, retain 512 minimum / 65,536 maximum and restore 32,768 default. More than 16,384 active quads render their own vertices; native quad/triangle appearance and FTE pool stay unchanged. |

## Reviewed rendering correction awaiting qualification

**Q01 — XR-011/PERF-021:** the minimal non-OIT stereo transparency/water-boundary
correction is source-integrated in `a7d06c01` following its committed before-code
plan. The audited renderer sorted from one origin and partitioned water from
the center leaf; OIT bypasses this sorting, but OIT-off remains allowed. Primary
deliberately shares alpha-sort origin, so shared sorting alone was not a
demonstrated regression. The adapter retains that shared sorting and partitions
the existing alpha stages using each eye's wet/dry category. It preserves opaque
single-pass stereo, without forcing OIT or adding another sorter.

The subsequent [Astra design disposition](stereo-water-transparency-2.0-review.md)
adopts per-eye category participation in the existing alpha stages, retaining
the 160-byte stereo uniform layout, shared sorting and opaque single-pass stereo.
Exceptional recording must be serialized to avoid duplicate entity cache writes;
repeated local-pitch mutation and persistent context reset must be addressed at
their existing owners. These changes are implemented and main-source-reviewed,
but rendered acceptance remains pending. The original crosswalk Q classifications
remain the frozen audit snapshot, not a claim that the design is still undecided.

All input, weapon, tracker, UI, base, co-op and platform rows now have explicit
source dispositions. Their software qualification remains required. No separate
two-hand gripping solver or replacement projectile/movement subsystem was
established as missing.

## Consolidated final software and delivery work

Only after all required implementation is finished:

1. Build the complete Linux x86-64 client/dedicated configuration and an isolated
   native Linux ARM64 client snapshot via ssh Foundry, with required OpenXR,
   shaders, codecs, CURL and Steam Audio 4.8.1 dependencies. Preserve the deployed
   game/server. Windows stays a release target; Windows builds are deferred.
2. Qualify ordinary desktop campaigns/mission packs, native controls/graphics/AO/
   demos/config ordering, shared features, clean mode changes and teardown.
   Desktop graphics use vkQuake; no VR demo gate.
3. Qualify the two-eye renderer and output: independent transforms, conservative
   either-eye visibility, native materials/particles/transparency/water, MSAA,
   post-effects and quality-selected VR AO, tracked HUD/console/menu/wheel,
   outline/name layers, avatar/prop/muzzle/shadow agreement and lifetimes.
   Include session/frame acquire-wait-release ordering, focus and mode changes,
   optional eye-toggle behavior, FB/META preference and allowed startup KHR
   fallback, fresh/invalid/stale gaze policy and explicit-only fixed mode.
   Runtime borrowed-image assumptions remain documented target-qualification
   limits; software fixtures do not certify a headset's allocation contract.
   Helpers/counters alone are not rendered-output acceptance.
4. Qualify software network behavior with desktop/VR clients and native/public/
   private peer profiles: prediction/reconciliation, loss/reordering/ACKs/splits,
   QC events and entities, voice framing/generation, metadata, slot reuse,
   teleport/death/respawn, mod install/switch/reconnect failure and cancellation.
   Cover the shared held/muzzle calibration, paired ranged inputs, gesture-only
   melee trigger/ready-pose policy, default-device VR mic opt-out and spatial
   loop/monitor policies using software-controlled inputs.
5. Qualify QC dispatch/permissions and actual supported content, file/buffer/
   search/string/entity/reflection/surface lifetimes, saves/hubs/co-op inventory/
   policy/callbacks and malformed-input cleanup. Use existing owners and meaningful
   end-to-end cases; registry-name counts are insufficient.
6. Load/leave representative large and supported mod maps, including Mjolnir
   mj4m1 and the requested jumbo-map cases, without a heapsize workaround. Check
   actual load/render/cleanup correctness; performance measurements are excluded.
7. Qualify installed Linux/ARM resources, loader resolution, dependency architecture,
   ABI ceiling and notices from the intended distribution artifacts. Rehearse one
   substantive upstream vkQuake merge in a disposable checkout and document actual
   adapter/conflict owners; do not mutate main or rewrite migration history.
8. Close final software findings and obtain the final local Astra integration
   review. Existing older tests are evidence inputs, not acceptance of the current
   final tree. User headset/gaze/live-listening/live-multiplayer/performance trials
   remain outside this goal.

## Scope boundaries

Required: OpenXR and native desktop; Linux x86-64, native ARM client, eventual
Windows; Beyond2e/Monado and Steam Frame streamed/native; two views; FB/META
preferred with allowed startup/device KHR fallback; optional eyes and full-quality
invalid/unavailable-gaze fallback; fixed foveation explicit only; native vkQuake
graphics; one solo/MP weapon/muzzle calibration; default-system microphone and
saved VR default-on opt-out; gesture-only melee with physical trigger and visible
attack animation suppressed; requested native rendering/loading threading,
precision, asset formats and large-map mechanisms.

Excluded: quad views, skyrooms, Gorilla, instant stop, hand-swim propulsion,
physical-contact melee/parry/hybrids, Mjolnir dual-state weapons, imagedump, co-op
revival, VR/additional demos, old setting/MP-offset aliases, general incompatible
device-loss reconstruction. AV-009 remains preserved research, not a general VRM
importer requirement. The eleven useful-addition candidates remain mapped optional
research; they were not blanket-approved implementation.

## Review evidence and execution order

- [Complete 185-row original audit crosswalk](final-scope-enumeration-2.0-worksheet.csv):
  snapshot classification and actual owner/evidence or exact question for every row.
  Older status/route columns remain historical, explicitly not proof.
- [Final current senior disposition](final-checklist-refresh-2.0-review.md):
  exhaustive remaining implementation substeps, source-verified refinements and
  current source-integrated versus qualification status.
- [Senior-review dispositions](final-scope-senior-disposition-2.0.md): adopted,
  adapted and rejected recommendations, source spot-checks and scope rationale.
- [Lead source returns](final-scope-lead-source-review.md) and
  [bounded audio/avatar/QC returns](final-scope-sidecar-source-review.md).
- [Supplemental interfaces/history](final-scope-interface-history-2.0.md): all
  interface, preservation and history inventories plus legacy/native/research
  dispositions. Names and routes alone do not certify runtime semantics.

Q01, C07, C10–C14, C18 and C21 are source-integrated. Finish C02 at native
metadata boundaries; C19 through
native packaging. Reuse reference
code and write a bounded plan before each major slice. Then perform the one
consolidated Linux/ARM qualification phase above and review its final fixes.
Source-present features should not be ported again.

Official runtime/API evidence is retained in
[the foveation plan](openxr-foveation-selection-2.0-plan.md) and
[the Frame input review](migration-steam-frame-foveation-review.md), including
[Valve's custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
and [OpenXR API documentation](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html).
Actual headset, gaze-provider and performance outcomes remain user-side work.

This is the final checklist against the pinned migration scope, not a claim that
implementation or executable parity is finished. If consolidated qualification
reveals another defect, record it against its existing feature/acceptance owner;
do not silently expand the feature scope.
