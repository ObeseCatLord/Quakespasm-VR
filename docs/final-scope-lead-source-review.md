# Local Astra xhigh source review returns

Review inputs at production2b420380. Current effective Astra/xhigh routing verified by main; older unavailable-routing statements below are superseded. Only source-review terminal results are reproduced, no operational telemetry. Intermediate coverage counts are historical.

This is a **partial review return: 66 IDs dispositioned, 119 unreviewed**. A bounded continuation is required before publishing the full185 checklist. MOD-011’s two defects are confirmed; its remaining physics contracts are not yet fully reviewed.

Requested provenance: local Astra xhigh source advice. Effective routing metadata is unavailable; this is not certified model signoff. No edits, builds, tests, probes, branch checks, telemetry access or delegation occurred.

The confirmed implementation checklist remains:

1. **M-PHYS-01 — P2, MOD-011: release invalid Toss supports after Think.** Current [sv_phys.c:11745](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:11745) returns whenever `FL_ONGROUND` remains set. Primary `51b452c0`, `sv_phys.c:6767`, validates non-world support references and releases hidden/freed/invalid supports after Think. Smallest seam: reuse that validation and clear grounded state immediately before the grounded return. Existing robust-pusher maintenance does not cover this lifecycle.
2. **M-PHYS-02 — P2, MOD-011: relink successful legacy elevator recovery.** Current [sv_phys.c:3279](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:3279) changes final Z and continues without updating spatial linkage. Primary `51b452c0:sv_phys.c:4414` and QSS-M `03a498:sv_phys.c:922` relink without retriggering touches. Smallest seam: `SV_LinkEdict(check, false)` on successful legacy recovery.

No additional **confirmed** missing feature emerged from this bounded coverage. That conclusion does not extend to the unreviewed IDs.

Several dispositions should change how main frames the remaining work:

- **WPN-010 is S.** I traced calibrated command production, finite decoding, accepted-command Think/PostThink consumers, source-expression compensation, world clamping, stock-nail correction, relocation/link handling and guarded restoration. The primary uses the same general QC source-compensation approach. Actual projectile outcomes and nested/cancelled-QC behavior remain qualification work; no replacement projectile subsystem is justified.
- **NET-017 is S with an explicit rejected subrequirement.** Current receive selection implements unique plausible rebinding and prior-endpoint protection. [The NAT review](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/docs/migration-nat-demux-review.md:23) expressly rejects three-second same-IP established-client pruning. Current native timeout is deliberate policy, not equivalent fast pruning and not missing implementation. Correct the stale inventory wording; add no same-IP eviction.
- **AV-009 is R.** Primary’s fingerprinted Alicia OpenGL/CPU sample is an experiment. Its existence establishes neither general VRM support nor a requirement to add an importer. The conditional manual-asset/performance preference must remain distinct from production avatar functionality.
- The current native lightmap sorter is text-identical to vkQuake `4bc898f2`; its grouping feeds native packing/update machinery. Another packer, allocator, scheduler or no-VIS cache is not justified by inventory wording.
- Correct preservation/history totals are **906 / 540**, with **497 MAIN + 43 XR** history entries. These are documentation corrections, not defects.

Four reviewed IDs remain **Q**, representing three exact questions:

- **XR-011 and PERF-021:** opaque multiview is integrated, but the advertised transparency exception contract needs reconciliation. `R_SortAlphaEntitiesTask` uses one center-origin order; `R_DrawAlphaEntitiesTask` uses the center leaf’s water classification; render-pass view masks broadcast to both eyes. Primary also deliberately uses a shared stereo sort origin, so shared sorting alone is not a demonstrated regression. Resolve the required non-OIT, eye/water-boundary behavior against native and primary references before proposing another sorter.
- **PERF-F001:** native whole-frame/AO timestamps and renderer counters exist. The inherited named-eye/stage capture and current shared-work/invalid-sample reporting have not been matched to an explicit accepted diagnostic contract. Do not manufacture separate eye timings for shared multiview work.
- **MOD-014:** native entity-field collection and drawing exist, but the field panel draws outside a tracked UIPanel. Resolve whether native screen-space diagnostics satisfy the selected baseline or whether inherited physical panel placement survives as a requirement.

The compact crosswalk below covers only the 66 dispositioned IDs. **S means integrated source ownership with software qualification pending**, not executable completion. Paths are relative to `quakespasm-2.0/Quake`, except `Shaders/` and explicitly pinned references.

| ID | Class | Actual owner / evidence |
|---|---|---|
| VR-001 | S | `gl_vidsdl.c:4746,4821,5181`: explicit attach/queued toggle, joined detach, input release and desktop fallback. |
| VR-002 | S | `vr_openxr.cpp:984,1077,1341`: pose/action sampling, reference/session events, frame reset; renderer retirement at `gl_vidsdl.c:4725`. |
| VR-003 | S | `vr_openxr.cpp:984` → `vr_openxr_math.h:52` → `gl_rmain.c:811` → `Shaders/stereo.inc`; independent runtime eye transforms and compositor poses. |
| VR-015 | S | `gl_vidsdl.c:4965,5026,5534,5558,5593`: optional mirror, submission ordering and conservative hidden-area eligibility. |
| VR-016 | S | `gl_vidsdl.c:1525,1558,2749`; `r_passes.c:1152`: native formats/samples, two-layer targets and resolve topology. |
| WPN-010 | S | `vr_input.c:4650,4793`; `sv_user.c:733`; `sv_phys.c:4367,4782,4909,8446`; `vr_weapon_calibration.c:3215`; `pr_cmds.c:280`. |
| COOP-007 | X | Current scope explicitly excludes revival and its callbacks; ordinary respawn remains elsewhere. |
| MOVE-002 | X | Physical reach/contact solver deferred by gesture-only decision. |
| MOVE-004..007 | X | Exact physical/native hybrid attack adapters deferred by the same decision; ordinary mod/native play survives separately. |
| MOVE-009..010 | X | Gorilla propulsion/contact/platform requirements explicitly deferred. |
| MOVE-012 | X | Gorilla contribution/ACK integration explicitly deferred; retained wire code does not create an acceptance gate. |
| AV-009 | R | Primary `51b452c0:r_alicia_spike.c:70,80,261`: explicit asset argument, fingerprint admission, OpenGL drawing; custom-avatar checkpoint preserves experiment provenance. |
| MOD-003 | S | `pr_ext.c:5439,5523,5552,6314`; `gl_draw.c:856`: registered client handlers, source clipping, padded subpictures and alpha submission. |
| MOD-007 | X | Skyrooms explicitly excluded. |
| MOD-011 | M | `sv_phys.c:3279,11745`; two confirmed defects above. Remaining ladder/customphysics/velocity-retention clauses await continuation. |
| MOD-014 | Q | `gl_rmain.c:1884`; `gl_screen.c:1136,2447`; `Shaders/basic.vert:36`: native diagnostics integrated; physical-panel contract unresolved. |
| XR-001 | S | `vr_openxr.cpp:1238,1490,1534,1638`; native renderer creation callers at `gl_vidsdl.c:1339,2379`: runtime GPU/creation metadata and compatible adoption. |
| XR-002 | S | `vr_openxr.cpp:1013,1028,1039,1077,1341`; `gl_vidsdl.c:5026`: bounded same-image waits, release eligibility, events and joined submission. |
| XR-004 | S | `vr_openxr.cpp:668,957,1299`; `gl_vidsdl.c:5243`; `vr_foveation_policy.h:31`: optional action/property admission, tracked finite gaze and freshness gates. |
| XR-005 | S | `gl_vidsdl.c:5091,5118`; `glquake.h:888`; `r_brush.c:1106`; `r_world.c:1452`: rate-map producer/upload and shared material eligibility. |
| XR-006 | S | `vr_openxr.cpp:1749`; `gl_vidsdl.c:5046`; `gl_rmain.c:2274`; `r_passes.c:1152`: paired centers, off restoration, fine-depth replay and protected resolve. Accepted runtime-image assumptions remain qualification limits. |
| XR-007 | S | `view.c:89`; `vr_foveation_policy.h:15,31`; `vr_openxr.cpp:1749`: explicit fixed mode, eye default and full-rate fallback. |
| XR-010 | S | `gl_rmain.c:811,2848`; `r_world.c:994`; `Shaders/indirect.comp:88`: shared preparation, either-eye PVS/face admission and native dependency graph. |
| XR-011 | Q | Multiview integrated; `gl_rmain.c:2345,2463` plus `r_passes.c:574` leave the exact non-OIT transparency exception contract unresolved. |
| XR-012 | S | `gl_rmisc.c:3197,3242,3337,3425,3482,5072`; native stereo descriptors at `gl_rmain.c:811`: bounded persisted cache, pipeline consumers and eager creation. |
| NET-017 | S | `net_dgrm.c:111,155,770,799,1811`; NAT review explicitly rejects fast same-IP pruning and retains native established timeout. |
| NET-025 | X | Gorilla-specific transport/authority work deferred; existing validation retained. |
| PERF-F001 | Q | `gl_vidsdl.c:4456,4509,4523`; `gl_rmain.c:2784`: native timestamps/counters present; inherited/adapted diagnostic reporting contract unresolved. |
| PERF-F002 | S | `gl_model.c:2458,2514,2640,2694,2748,3289`: initialized styles, bounded face/node references and exact-count native plane storage. |
| PERF-F003 | S | `r_brush.c:3109`; `gl_mesh.c:1622,1746`; `gl_rmain.c:2887`: frame palette dependencies, per-entity animated BLAS and native TLAS/lightmap consumers. |
| PERF-001 | S | `tasks.c:296,453`: queued scalar/indexed execution and actual worker creation. |
| PERF-002 | S | `gl_rmain.c:2848`: submitted native render/particle/lightmap/BLAS dependency graph, with serial path retained. |
| PERF-003 | S | `gl_model.c:1525,1847`: indexed texture jobs call native image/upload owners and join before return. |
| PERF-004 | S | `gl_model.c:4470,6609,6760`: MDL/MDX indexed skin jobs, distinct slots and joined temporary-name lifetime. |
| PERF-005 | S | `gl_model.c:2595`: indexed surface-extents jobs with native worker-call serial fallback. |
| PERF-006 | S | `mem.c:88,120`; native model/brush callers use dynamic allocation rather than a replacement fixed hunk. |
| PERF-007 | S | `gl_model.h:795`; `gl_model.c:2973`; native visibility consumers dereference owning-surface indices. |
| PERF-008 | S | `r_brush.c:2795`: ordinary uploaded polygons released; tiled consumers retained and BSP geometry remains available. |
| PERF-009 | S | `r_brush.c:1994,1310`: native sorter and shelf/bin packing; sorter matches vkQuake `4bc898f2:1799`. |
| PERF-010 | S | `gl_model.c:3716,3742`: native BSP29/2PSB/BSP2/Valve/Quake64 dispatch. |
| PERF-011 | S | `mem.c:88,120` and native loader allocation paths; named-map load/exit without heapsize remains software qualification. |
| PERF-012 | S | `gl_model.c:1847,2595,4470,6760`: actual parallel loading mechanisms; reduced elapsed time is unmeasured and not a required measurement gate. |
| PERF-013 | S | `r_world.c:994`; stereo frusta in `gl_rmain.c`; `Shaders/indirect.comp:88`: conservative visibility independent of gaze. |
| PERF-014 | S | `r_brush.c:1053`; `r_world.c:1234`: native indirect/direct material, atlas, alpha and special-surface boundaries. |
| PERF-015 | S | `r_brush.c:3912`; `Shaders/update_lightmap.inc:289`: native dirty-lightmap work and either-eye lighting admission. |
| PERF-016 | S | `r_world.c:994`: native no-VIS/PVS and worker/serial owners retained; no claim of equal cost to primary’s specialized GL cache. |
| PERF-017 | S | `r_alias.c:91,110,135,397`; `gl_rmain.c:1216,1275`: bounded MDL/MD3 instance batching, state matching and immediate-draw exclusions. |
| PERF-018..019 | S | `gl_vidsdl.c:1525,2749`: native higher-precision color/depth selection and stereo attachment consumers. |
| PERF-020 | S | `Shaders/world.vert:45`; `r_brush.c:1152`: eligibility-masked clip bias; matches Ironwail `08d57813:gl_shaders.h:518` reversed-Z formula. |
| PERF-021 | Q | Opaque multiview exists through `Shaders/stereo.inc` and native view-mask passes; shares XR-011’s unresolved transparency contract. |
| PERF-022 | S | `r_alias.c:1157`: loaded offscreen aliases rejected before skin/pose preparation; mj4m1 correctness remains qualification. |
| PERF-023 | S | `gl_mesh.c:1491,1662,1767`: exact untracked pose-cache reuse; tracked palettes bypass cache. Also existing polygon release/early alias rejection. |
| ASSET-001 | S | `image.c:157`: path-priority-first PNG/TGA/JPG/JPEG selection and native decoder dispatch. |
| ASSET-002 | S | `gl_model.c:6609,8059`: MD3 naming fallbacks feed shared native skin/material worker. |
| ASSET-003 | S | `gl_model.c:6609,7236`: MD5 skin producer feeds the same native worker. |
| ASSET-004 | S | `gl_model.c:6609`: indexed fullbright scan and glow/luma lookup; native alias fullbright descriptors at `r_alias.c:391`. |
| ASSET-005 | S | `gl_model.c:1525`: map-specific/global truecolor and glow/luma precedence through native texture ownership. |
| ASSET-006 | S | `gl_model.c:1540` static-model path floor; shared native brush/alias format and skin loaders. |
| ASSET-007 | S | `gl_model.c:1228,1280,1706,1741`: bounded external WAD lookup, palette payload and native indexed-palette upload. |
| ASSET-008 | S | `gl_model.c:2539`; `r_world.c:1395`; `r_brush.c:1156`: lit-liquid surface flags and native atlas/draw consumption. |
| ASSET-009 | S | `gl_rlight.c:41`; `r_brush.c:1231,3912`: shared lightstyle values reach CPU/GPU update owners. |

All **119 unreviewed IDs** remain outside this return’s source conclusions:

| Family | Exact unreviewed IDs |
|---|---|
| BASE | BASE-001..003 |
| VR | VR-004..014 |
| WPN | WPN-001..009, WPN-011..012 |
| COOP | COOP-001..006, COOP-008..013 |
| MOVE | MOVE-001, MOVE-003, MOVE-008, MOVE-011 |
| AV | AV-001..008 |
| FBT | FBT-001..005 |
| MOD | MOD-001..002, MOD-004..006, MOD-008..010, MOD-012..013 |
| UI | UI-001..006 |
| AUDIO | AUDIO-001..011 |
| XR | XR-003, XR-008..009 |
| PLAT | PLAT-001..008 |
| NET | NET-001..016, NET-018..024, NET-026..029 |

Separate qualification/delivery obligations remain open:

1. Finish source coverage, resolve Q contracts and reconcile all **512 QC / 1303 command-cvar** rows through actual dispatch/consumer or explicit disposition. This pass did not independently complete those ledgers or all preservation/history obligations.
2. After implementation ends, perform consolidated Linux x86-64 and isolated native Foundry ARM64 builds, dependencies, generated shaders, packages and software behavior qualification. Preserve the running server.
3. Require actual rendered scene/material/eye output and lifecycle behavior, native QC/network/save outcomes, representative mod/large-map loading and cleanup. Helpers, counts and packet captures alone are insufficient.
4. Retain Windows as an eventual release target. Windows builds and user headset/gaze/live-multiplayer/performance trials remain deferred.

Excluded work remains quad views, skyrooms, Gorilla/instant-stop/swim propulsion, physical-contact melee/parry and deferred hybrid adapters, Mjolnir dual-state weapons, imagedump, revival, VR demos/additional demo features, legacy-setting/MP-offset aliases and general incompatible-device reconstruction. Optional background saves, retry redesign, previews, downloads/dialects/CSQC prediction/ICE/255 slots, dithering and clustered lighting remain research proposals.

Supplemental desktop disposition: primary’s later-opposing-key `cl_iDrive` behavior differs from current native subtraction. The selected vkQuake desktop baseline supports retaining native behavior; record that explicitly without declaring BASE-003 fully reviewed. Unconsumed `cl_mwheelpitch`, unsupported `r_bloodstains`, waived hover controls and renamed movement-variable producers are not established missing features.

**Next bounded continuation:** NET-001..009, NET-016, NET-018..022 — 15 IDs covering protocol/decoder, reliable transport, discovery and reconnect contracts against the pinned native/primary/QSS references. Reuse this reviewer; carry the three Q questions and MOD-011’s remaining clauses forward without coding.

---

**Interim checkpoint: 65 IDs have completed disposition reviews; MOD011 has two confirmed missing subfeatures but an unfinished remainder; 119 IDs remain unclosed. Full185 coverage is not complete.** No edits or execution checks were performed. Completed evidence and the partially reviewed network tranche remain available for continuation.

“S” means current source integration was traced through relevant owners/callers/gates; it does **not** mean executable qualification is complete.

| Disposition | Exact IDs |
|---|---|
| **S — source integrated/native; qualification pending (49)** | VR001–VR003, VR015–VR016; WPN010; MOD003; XR001–XR002, XR004–XR007, XR010, XR012; NET017; PERF001–PERF020, PERF022–PERF023; PERF-F002–PERF-F003; ASSET001–ASSET009 |
| **Q — exact unresolved source contract (4)** | MOD014; XR011; PERF021; PERF-F001 |
| **R — reference experiment (1)** | AV009 |
| **X — excluded (11)** | COOP007; MOVE002, MOVE004–MOVE007, MOVE009–MOVE010, MOVE012; MOD007; NET025 |
| **M — confirmed missing, row remainder still open (1)** | MOD011 |

The **119 IDs without a completed row-level review** are exactly:

| Family | Unclosed IDs |
|---|---|
| BASE | BASE001–BASE003 |
| VR | VR004–VR014 |
| WPN | WPN001–WPN009, WPN011–WPN012 |
| COOP | COOP001–COOP006, COOP008–COOP013 |
| MOVE | MOVE001, MOVE003, MOVE008, MOVE011 |
| AV | AV001–AV008 |
| FBT | FBT001–FBT005 |
| MOD | MOD001–MOD002, MOD004–MOD006, MOD008–MOD010, MOD012–MOD013 |
| UI | UI001–UI006 |
| AUDIO | AUDIO001–AUDIO011 |
| XR | XR003, XR008–XR009 |
| PLAT | PLAT001–PLAT008 |
| NET | NET001–NET016, NET018–NET024, NET026–NET029 |

Additionally, **MOD011’s remaining physics obligations are unreviewed**, despite its established M classification. Reading portions of a row has not promoted it into completed coverage.

Two missing features are confirmed, both **MOD011 / P2**:

| Finding | Current evidence and pinned reference | Smallest seam |
|---|---|---|
| **Post-Think toss ground validity** | Current [sv_phys.c:11745](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:11745) returns on `FL_ONGROUND` after Think without the reference’s support-validity check. Primary `51b452c0`, `Quake/sv_phys.c:6767–6804`, validates world/nonworld ground references and clears invalid grounded state. Existing robust-pusher bookkeeping is not this post-Think check. | Reuse the primary validity check immediately before the grounded return; preserve native toss ownership. |
| **Legacy elevator successful nudge lacks relink** | Current [sv_phys.c:3279](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:3279), in the non-robust elevator mode 1/2 path, adjusts final Z and continues after a clear position check without relinking. Primary `51b452c0`, `sv_phys.c:4414`, and QSS-M `03a498`, `sv_phys.c:922–930`, relink before continuing. The robust path already relinks. | Add the reference’s non-triggering relink at that successful legacy-path exit. |

**No additional M finding is established by this checkpoint.** These are source findings; no fixes or runtime reproductions were attempted.

The four Q dispositions remain narrowly defined:

| IDs | Evidence and unresolved contract |
|---|---|
| **XR011, PERF021** | Current `gl_rmain.c:2345` sorts alpha entities from one center origin; `:2463` uses the center view leaf for water categorization. `gl_rmisc.c:114` gates sorting against native OIT. Primary `51b452c0`, `gl_rmain.c:1169`, also deliberately uses a shared stereo sort origin. Resolve the promised per-eye transparency/water-boundary behavior against the native OIT baseline. Shared sorting alone does **not** establish a missing feature or justify a new sorter. Opaque multiview integration is supported. |
| **MOD014** | Native diagnostic collection/gating exists at `gl_rmain.c:1884`; display exists at `gl_screen.c:1136,2447`. `DrawInfoPanel` uses the default canvas, while `Shaders/basic.vert:36` applies stereo placement through the panel flag. Resolve whether a physically placed VR developer panel is a surviving requirement. No missing diagnostic collector is established. |
| **PERF-F001** | Native frame/AO timestamps exist at `gl_vidsdl.c:4456,4509,4523`, with reporting at `gl_rmain.c:2784`. The inherited named-eye/stage reporting contract has not been reconciled precisely with shared multiview work and invalid/unavailable timing presentation. Do not invent separate eye timings for shared passes. |

I read **`docs/final-scope-interface-history-2.0.md` completely** and integrated these dispositions:

- **Opposing keys:** retain native vkQuake subtraction. Primary `51b452c0`, `cl_input.c:418–431`, implements later-key arbitration through `CL_KeyStatePair` with `cl_iDrive`. Pinned vkQuake `4bc898` uses independent subtraction, as current `cl_input.c:540,568–569` does. The explicit native desktop baseline supersedes primary arbitration; no alias or adapter is required. This resolves the sub-obligation, **not all of BASE003**.
- **NET017:** retain native established-client timeout. Current `net_dgrm.c:111–195` provides validated, unambiguous same-host port remapping and old-endpoint guards. `docs/migration-nat-demux-review.md:23` explicitly rejects primary three-second same-IP established-slot pruning because it can evict legitimate quiet players. Reconcile stale inventory acceptance wording; **do not add same-IP eviction**.
- **AV009:** preserved OpenGL CPU sample remains R. It establishes neither a production general VRM importer obligation nor permission to impose mandatory CPU retargeting irrespective of the user’s conditional performance preference.
- Correct ledger counts are **906 preservation rows**, **540 history rows (497 MAIN / 43 XR)**, **1303 interface rows**, and **512 QC rows**. These are documentation/accounting corrections, not production defects. Reading the supplemental reconciliation is not an independent semantic audit of every ledger row.
- Absent aliases, declaration-only `cl_mwheelpitch`, native menu hover, existing movement defaults/info/stats, and unsupported conditional blood-stain code do not independently establish missing product behavior. Historical deletions and WIP research remain evidence requiring dispositions, not automatic restoration requirements.

The next tightly bounded tranche should remain the **15 already in progress**:

**NET001–NET009, NET016, NET018–NET022.**

Current and pinned protocol negotiation, replacement snapshots, CSQC entity transport, recipient visibility, and split-packet ownership have substantial retained source evidence. Before closing these rows, finish their exact acceptance mapping and remaining consumers—especially private movement dispatch, complete owner/stat receipt handling, CSQC enable/remove ordering, native network-driver equivalence, discovery, and reconnect/install lifecycle. **None of these 15 has been counted as completed yet.**

Estimated scope is **one bounded continuation for those 15 dispositions**, returning unresolved contracts explicitly if necessary. If all close, coverage becomes **80 completed disposition reviews + MOD011 partial + 104 unclosed IDs**. Subsequent tranches are still required for the final185 checklist.

All S rows retain their applicable software qualification and delivery gates. Source presence, packet plumbing, and counters are not user-observable completion; Windows target builds and user headset/gaze/multiplayer/performance qualification remain separately deferred under the existing policy.

This is the same continuing reviewer’s requested Astra xhigh source advice; effective routing metadata is unavailable, so this is not a certified model sign-off.

---

**Coverage remains 65 completed disposition reviews, MOD011 partially reviewed with two confirmed missing features, and 119 unclosed IDs. No full185 completion claim. No edits or execution checks performed.**

For this review, **main’s local route verification establishes `gpt-6-astra`, effort `xhigh` provenance**. This supersedes the earlier routing caveat; it does not establish full-scope completion or qualification.

| Classification | Exact reviewed IDs |
|---|---|
| **S — source integrated/native; qualification pending (49)** | VR001–VR003, VR015–VR016; WPN010; MOD003; XR001–XR002, XR004–XR007, XR010, XR012; NET017; PERF001–PERF020, PERF022–PERF023; PERF-F002–PERF-F003; ASSET001–ASSET009 |
| **Q — unresolved source contract (4)** | MOD014; XR011; PERF021; PERF-F001 |
| **R — reference experiment (1)** | AV009 |
| **X — excluded (11)** | COOP007; MOVE002, MOVE004–MOVE007, MOVE009–MOVE010, MOVE012; MOD007; NET025 |
| **M — confirmed missing; remainder of row unreviewed (1)** | MOD011 |

Exact **119 unclosed IDs**:

| Family | IDs |
|---|---|
| BASE | BASE001–BASE003 |
| VR | VR004–VR014 |
| WPN | WPN001–WPN009, WPN011–WPN012 |
| COOP | COOP001–COOP006, COOP008–COOP013 |
| MOVE | MOVE001, MOVE003, MOVE008, MOVE011 |
| AV | AV001–AV008 |
| FBT | FBT001–FBT005 |
| MOD | MOD001–MOD002, MOD004–MOD006, MOD008–MOD010, MOD012–MOD013 |
| UI | UI001–UI006 |
| AUDIO | AUDIO001–AUDIO011 |
| XR | XR003, XR008–XR009 |
| PLAT | PLAT001–PLAT008 |
| NET | NET001–NET016, NET018–NET024, NET026–NET029 |

**MOD011’s remaining physics obligations also remain open.** Partial source reads have not been counted as completed row reviews.

Confirmed missing features remain these two **MOD011 / P2** items:

| Finding | Evidence | Smallest seam |
|---|---|---|
| Post-Think toss ground validity | Current [sv_phys.c:11745](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:11745) returns on grounded state after Think without validating continued support. Primary `51b452c0`, `sv_phys.c:6767–6804`, validates ground references and clears invalid support. Robust-pusher bookkeeping is not equivalent. | Reuse the primary support-validity check immediately before the grounded return. |
| Legacy elevator successful nudge lacks relink | Current [sv_phys.c:3279](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:3279) continues after a successful legacy elevator Z adjustment without relinking. Primary `51b452c0`, `sv_phys.c:4414`, and QSS-M `03a498`, `sv_phys.c:922–930`, relink there. | Restore the non-triggering relink at that exit; retain existing pusher architecture. |

No additional M finding is established.

The unresolved source contracts remain:

| IDs | Evidence and required resolution |
|---|---|
| XR011, PERF021 | Current `gl_rmain.c:2345,2463` uses center-origin alpha sorting and center-leaf water categorization; `gl_rmisc.c:114` gates sorting against OIT. Primary `51b452c0`, `gl_rmain.c:1169`, also intentionally shares a stereo sort origin. Resolve the promised per-eye transparency/water-boundary behavior against native OIT. Shared sorting alone does not justify a new sorter. |
| MOD014 | Collection and gates exist at `gl_rmain.c:1884`; presentation at `gl_screen.c:1136,2447`. Default-canvas drawing versus panel-flag stereo placement in `Shaders/basic.vert:36` leaves the physical VR developer-panel obligation unresolved. No missing collector is established. |
| PERF-F001 | Frame/AO timestamps exist at `gl_vidsdl.c:4456,4509,4523`, reporting at `gl_rmain.c:2784`. Reconcile inherited eye/stage reporting with shared multiview work and unavailable-timing presentation. Do not invent separate eye timings for shared passes. |

The supplemental interface/history document was read completely. Its consequential dispositions are retained:

- **Opposing keys:** native vkQuake subtraction governs. Primary `51b452c0`, `cl_input.c:418–431`, has later-key arbitration; pinned vkQuake `4bc898` and current `cl_input.c:540,568–569` use independent subtraction. Primary arbitration is superseded by the explicit native desktop baseline. No alias or adapter is required; BASE003 otherwise remains open.
- **NET017:** native timeout remains intentional. Current `net_dgrm.c:111–195` provides guarded port remapping. `migration-nat-demux-review.md:23` explicitly rejects three-second established same-IP pruning. Stale inventory wording needs reconciliation, not new eviction code.
- **AV009:** reference CPU sample; no general production VRM importer obligation established.
- Counts are **906 preservation**, **540 history = 497 MAIN + 43 XR**, **1303 interface**, and **512 QC** rows. Accounting corrections and absent literals are not production defects. Supplemental-ledger reading is not independent behavioral verification of every row.

**Next bounded continuation: NET001–NET009, NET016, NET018–NET022 — exactly 15 IDs.** Existing protocol, snapshot, CSQC transport, visibility, and split-packet evidence is retained. Remaining checks concern acceptance mapping and consumers, including movement dispatch, owner/stat receipt handling, CSQC lifecycle ordering, driver equivalence, discovery, and reconnect/install lifecycle.

Estimate: **one bounded continuation for this tranche**, with Q dispositions returned wherever exact contracts remain unresolved. Closing all 15 would produce **80 completed disposition reviews + MOD011 partial + 104 unclosed IDs**. Further tranches remain necessary.

All S classifications retain applicable qualification/delivery gates. No tests, builds, runtime outcomes, or user headset/gaze/multiplayer/performance results are implied by this checkpoint.

---

**This exact 15-ID tranche is reviewed: 12 S, 3 M, no new Q.** Cumulative coverage is **80 completed disposition reviews + MOD011 partially reviewed + 104 unclosed IDs**. Prior dispositions and evidence remain retained. No edits, builds, tests, runtime checks, or agents were used.

References below are current `Quake/` files. Pins: **F** primary `51b452c0`; **Q** QSS-M `03a498`; **V** vkQuake `4bc898`. **S means source integrated, with qualification pending.**

| ID | Class | Actual owner/caller/consumer evidence and contract disposition |
|---|---|---|
| NET-001 | **S** | `sv_main.c:2889,3394,3456,3672` selects NQ/Fitz/RMQ, intersects peer extensions and applies message/entity/resource limits; `cl_parse.c:2014,2135` validates and installs the selected protocol. Native negotiation retained; mixed-peer execution remains a gate. |
| NET-002 | **S** | `protocol.h:461`; `sv_main.c:3421` requires explicit private admission; `cl_parse.c:2027,2075` rejects unoffered/incompatible layouts; `sv_user.c:2108` and `cl_input.c:1190` select private movement only by that profile. Colliding public bits do not authorize private decoding. |
| NET-003 | **S** | `sv_main.c:1914,2256,2529,2669,4605` connects recipient customization/PVS, native owner preservation, delta calculation, removals/resets and extended indices; `cl_parse.c:831,1539` consumes them. Model-less emitter eligibility is transport-only: F `cl_main.c:2221` and Q `:2136`, like current `:2319`, reject model-null before emission. No new rendering contract inferred. |
| NET-004 | **S** | `sv_main.c:1590,1656,2201,4702` reuses transport-sequence history, repeats selected private owner/stats and bounds continuation progress. `cl_parse.c:1376,1485,3395` preserves ACK order and commits complete same-message receipts; `cl_input.c:812` drains ACKs; `sv_user.c:2139` receives them. |
| NET-005 | **S** | `common.c:1319,1560` implements matching coordinate/angle codecs; `sv_main.c:1427,2751` encodes solids; `cl_parse.c:810,1033` distinguishes public/private solid layouts; `pmove.c:2550` and `cl_main.c:1870` consume collision dimensions. |
| NET-006 | **S** | `sv_main.c:1059,1772,1819` produces typed stats and complete private movement stats; `cl_parse.c:1700,3101,3849` consumes numeric/string/velocity/ground state; `pmove.c:2704` consumes movement variables; `pr_ext.c:5217` exposes QC stats. Initial public metadata has the separate NET-021 defect below. |
| NET-007 | **M** | Staged sounds/statics/baselines are called at `sv_main.c:5291`; matching consumers at `cl_parse.c:2972,3869`. **Loading keepalives and QC late-model precaching have confirmed gaps below.** Model-name staging is intentionally disabled in current `sv_main.c:4934`, F `:4608`, and Q `:3636`; its empty stage is not an additional missing feature. |
| NET-008 | **S** | `cl_parse.c:1079` consumes the conditional absolute-light byte; `:3565` applies angle deltas, including tracked-view adjustment; `:3894` dispatches public CSQC events through the loaded VM. Absolute-light rendering is not established by byte consumption. Private PEXT1 remains intentionally zero. |
| NET-009 | **S** | `host.c:1064,1190` arms readiness before prespawn; `cl_main.c:2680,2765` retries bounded enable writes; `host_cmd.c:3731` activates negotiated transport. `sv_main.c:1633,1682,2368,4725` connects retirement/resend/callback handling; `pr_edict.c:170` supplies the free hook; `cl_parse.c:2327,2358,2438` owns mapped create/update/remove. NET-021 affects initial metadata availability, not the existence of this transport. |
| NET-016 | **S** | `net_dgrm.c:405,453,485,566,818` supplies fragmentation, ACK/EOM, retransmission, length checks and unreliable ordering. `net_main.c:203` clamps pending MSS; `net_dgrm.c:425` adopts it only at a new reliable message. Both shared-server and ordinary-client receive paths inspected. |
| NET-018 | **S** | `net_loop.c:33,71,136,184,217,268` resets sequences, preserves unreliable packet identity, reserves reliable buffer capacity and disconnects peers. `net_main.c:163` exposes matching sequence identity used by snapshot ACK ownership. |
| NET-019 | **S** | `net_bsd.c:31,43` and `net_win.c:31,44` wire actual drivers; `net_udp.c:157,633,729` supplies nonblocking IPv4/IPv6 and address parsing. All four driver files are byte-identical to V. Platform execution remains unqualified. |
| NET-020 | **M** | Native discovery/control owners are live: `net_main.c:391,527`; `net_dgrm.c:747,1481,1721,1923,2055,2530`. LAN/master queries, heartbeat, status, RCON and challenge/ProQuake handling exist. **The challenge offer advertises unsupported dialects**, detailed below. |
| NET-021 | **M** | Incremental producer `cl_main.c:2946`, receivers `:2854–2909`, registration `:3293`, and QC consumers `pr_ext.c:5718,5764,5787` exist. **Initial/full metadata publication, retirement clearing and generic SSQC lookup are incomplete**, detailed below. |
| NET-022 | **S** | `cl_parse.c:2158,2181,3551` bounds private server-directed switching and stops parsing after redirect. `cl_main.c:470,543,837,913,958` preserves endpoint/profile, approval, config/signon timing and cancellation. `host.c:1183` advances it; `menu.c:5818` invokes acceptance; `addon_catalog.c:430,663` owns validated installation/cancellation. Reconnect uses existing `NET_DatagramConnectStart/Frame/Cancel`, not another socket owner. |

**Confirmed missing code, ordered by practical impact; smallest reuse seams:**

1. **NET-007 — late QC model precache reaches the wrong client cache.** Builtin #20 maps to `PF_sv_precache_model` at [pr_cmds.c:2284](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/pr_cmds.c:2284). Its late-precache write at `:1540` uses the sound tag `0x8000`; `cl_parse.c:2977,3002` therefore selects sound loading. The correct existing `SV_Precache_Model` at `pr_cmds.c:1497` writes the model tag. **Reuse that helper from the QC wrapper**, matching Q `pr_cmds.c:1206,1239`, and remove duplicated precache logic.

2. **NET-021 — initial metadata and complete slot retirement are missing.** Current `sv_main.c:3578–3606` publishes no full serverinfo; `host_cmd.c:2423` initializes only names/colors/frags; `host.c:646` clears those same legacy fields on departure. Existing custom userinfo keys consequently lack full initialization/retirement. Q publishes serverinfo at `sv_main.c:2276`, full userinfo at `host_cmd.c:9802`, and clears it at `host.c:1437`. **Adapt those bounded writes at the existing signon/spawn/drop seams**, using recipient capabilities and current info stores. This also restores the initial serverinfo prerequisite for `cl_main.c:1145` userinfo enumeration.

3. **NET-021 — SSQC generic `infokey` lookup is absent.** Current [pr_ext.c:4464](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/pr_ext.c:4464) handles special keys but returns null for ordinary world/player keys at `:4478,4528`. Q `pr_ext.c:4622,4676` reads the respective info stores. **Reuse `Info_GetKey` in those two fallback branches.** Existing `serverkey` and CSQC player-key functions do not satisfy calls to this SSQC builtin.

4. **MOD011 / P2 — post-Think ground validity**, carried unchanged. Current `sv_phys.c:11745` lacks F `sv_phys.c:6767–6804` support validation. **Reuse that check before the grounded return.**

5. **MOD011 / P2 — legacy elevator relink**, carried unchanged. Current `sv_phys.c:3279` continues after the successful Z nudge without relinking; F `:4414` and Q `:922–930` relink. **Restore the non-triggering relink at that exit.** MOD011’s remaining obligations stay open.

6. **NET-007 — client loading keepalives are absent.** Current [cl_parse.c:1908](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/cl_parse.c:1908) compiles out the helper; loading loops at `:2281,2290` never call it. F calls its active helper at `cl_parse.c:864,871`. NET-007 explicitly retains this behavior. **Adapt the primary main-thread loading callback around existing precache operations**, preserving parser/reliable-message state. Server-originated nops are not client-originated keepalives. No loading timeout was reproduced.

7. **NET-020 — advertised dialects exceed decoder support.** Current [net_dgrm.c:2672](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/net_dgrm.c:2672), inherited from V `net_dgrm.c:2102`, offers DP7/BJP3; current `cl_parse.c:2060` accepts only NQ/Fitz/RMQ. **Remove unsupported offer tokens within the existing challenge handler.** DP7/BJP3 implementation remains optional research.

Qualification remains separate: mixed public/private peers, loss/reordering/split recovery, observable CSQC lifecycle, slow/large loading, metadata join/map-change/slot-reuse behavior, IPv4/IPv6, and install/cancel/reconnect outcomes still require software acceptance. NET-022 installation requires the CURL-enabled delivery configuration (`meson.build:395`); DNS remains synchronous. Windows builds and user live qualification retain their existing deferred dispositions.

The prior four Q rows remain open. Your timing-availability evidence is retained for PERF-F001’s final contract disposition; no fabricated per-eye split is proposed.

The next non-overlapping network tranche is **NET-010–015, NET-023–024, NET-026, NET-028–029: 11 IDs**. NET-027 remains with the audio sidecar. Full185 completion is still outstanding.