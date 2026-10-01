# Local Astra xhigh source review returns

Review inputs at production2b420380. Effective Astra/xhigh routing verified by main; older unavailable-routing statements below are superseded. Only reviewer source-review results and explicit bounded disposition tables are reproduced, no operational telemetry. Intermediate coverage counts are historical. Main corrected the final crosswalk's r_light.c:41 receipt to the actual Quake/gl_rlight.c:41; source-return wording below is retained. Final reconciliation:153 S /18 M /2 Q /11 X /1 R =185 unique IDs.

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

---

**This tranche is source-reviewed: 9 S, 2 M, 1 Q. MOD-011’s remainder is closed at source-review level. NET-022 changes from S to M.** Prior evidence and dispositions otherwise remain retained.

Pins: **F** `51b452c0`, **QSS** `03a498`, **V** `4bc898`. Current references below are under `Quake/` at the supplied production snapshot `2b420380`. **S means source-integrated, with qualification pending.**

| ID | Class | Actual owner, caller/consumer evidence and disposition |
|---|---|---|
| NET-010 | S | `cl_input.c:1100,1152` records commands, suppresses the first two, sends current plus two predecessors. `sv_user.c:1440` expands sequence wrap and rejects duplicates after decoding; `:1199,1232,1572` owns queue/retirement. Current capacity is **32 records/250 ms**, `server.h:179`, versus F’s 24; preserve the bounded current queue, not the historical number. |
| NET-011 | S | `cl_input.c:843` samples monotonic elapsed time with fractional carry and clamps before integer conversion; `sv_user.c:657,1202` validates 1–125 ms. `sv_phys.c:9899,10755` consumes/debits explicit command duration. Matches F `cl_input.c:718`’s duration contract; timestamps do not donate a lost interval. |
| NET-012 | S | Native scheduler `host.c:156,187,1200,1235` plus `cl_main.c:2664,2705`: accumulated input, retained server catch-up, one private send per rendered frame. **F’s negative `host_maxfps` spelling is superseded** by native `host_phys_max_ticrate`; this is explicitly reconciled in `migration-command-source-checkpoint.md:28`. |
| NET-013 | S | `cl_main.c:1750,1838,1870,1907,1981,2143` consumes coherent authoritative state, collision hulls/movevars, command history and disposable pending input through shared PMove. `sv_phys.c:10389,10546,10618,10746` preserves generic QC velocity and performs sticky native ledge handoff. Ordinary swimming is present; arbitrary QC forces/trajectory parity are not established by these reads. |
| NET-014 | **M** | ACK authority, permission and epochs are integrated at `sv_main.c:2123`, `cl_parse.c:3146,3334`, `cl_main.c:1787`. **Inherited opt-in reconciliation smoothing lacks its producer and view consumer**; current `cl_main.c:275` only resets fields, and `:2059` directly publishes replay output. Primary quarantine/retry ownership is explicitly superseded; smoothing is a separate omission. |
| NET-015 | S | `cl_input.c:657,732` → `sv_user.c:794,1080` carries private QC inputs and button3–8. `pr_cmds.c:53` is called by CSQC input filtering at `cl_main.c:2737` and cooperative server input at `sv_phys.c:11112`. Builtin359 `pr_ext.c:5815,6341` writes bounded typed events; `sv_user.c:1940,2022,2147` resolves and invokes `CSEv_*`. F’s private extbits layout survives; QSS’s long-button layout is not required here. |
| NET-023 | S | `vr_input.c:5129,5146` → `cl_input.c:703` → `sv_user.c:733,1486`: relative muzzle/aim, finite checks and per-sample roomscale outlier rejection. `sv_phys.c:3763,4367` owns collision/body application and temporary weapon pose. Reuses F’s payload/validation contract without moving physics into receipt handling. |
| NET-024 | S | Surviving pose transport: `vr_input.c:4487,5159`, `cl_input.c:716`, `sv_user.c:760`, `sv_phys.c:4310,4401`. Contact framing/profile negotiation also exists at `sv_user.c:821`, `sv_main.c:5061,5274`, `cl_main.c:3193`. **Physical-contact damage/parry adapters remain excluded**; their wire fields do not restore that scope. Weapon-specific presentation remains in main’s separate review. |
| NET-026 | S | `cl_parse.c:166,224` capability retry is called by `cl_main.c:2714`; `cl_input.c:881` frames poses; `sv_user.c:1715,1877,2125` negotiates/decodes. `sv_main.c:540,590,625,4856` owns sequence, generation, expiry and relay; `cl_parse.c:474,588` admits/cache-publishes; `r_alias.c:640` consumes stale-checked poses. Avatar identity uses `cl_main.c:106,140,2711`, `sv_user.c:1636`, `sv_main.c:36,70,5365`, `cl_parse.c:313`. Codec/identity files directly reuse F; codec’s sole inspected difference admits v4. |
| NET-028 | **Q** | Lifecycle is integrated: `sv_phys.c:8763,9677,11219,11582`; builtin347 at `pr_ext.c:4673` calls `sv_phys.c:9456`; legacy taps latch at `sv_user.c:1523`. **Exact unresolved contract:** F `sv_pmove_policy.h:62` explicitly excludes single-player, while current admission `sv_main.c:863` lacks that guard. Ordinary local startup uses native profile (`host_cmd.c:3224`); explicit `qsvr1` selection is exposed at `:1656`. Decide whether exclusion must also cover explicit private-profile single-player. If retained literally, the smallest seam is the existing admission predicate—not another movement owner. |
| NET-029 | S | Tracking is consumed before the `MOVETYPE_NONE` return at `sv_phys.c:3770`, matching F `:6232`. Collision/pickup remain world-owned (`world.c:631,2734,2778,3642`); respawn completion precedes ACK publication at `sv_phys.c:11403–11422`. Teleport cleanup remains at `pr_edict.c:169`, `world.c:565`, `sv_phys.c:11562`. This closes network/physics ownership, not the separately queued co-op feature qualification. |
| MOD-011 | **M** | Native pusher dispatcher `sv_phys.c:2980,3344,12009`, support release/adoption `:2745,2797,11998`, rollback `:3312`; customphysics `:1802,11196,12004`; QC velocity retention `:10277,10334,10389`; ladder consumer `pmove.c:1465,2146` and native authority gate `sv_phys.c:8827`. These are integrated. **Two previously confirmed gaps remain:** post-Think toss support validity and successful legacy elevator-nudge relinking. |

Confirmed missing work and smallest seams:

- **NET-022/UI-004 failure path — confirmed amendment, P2.** [cl_parse.c:2286](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/cl_parse.c:2286) invokes `Host_Error`; `host.c:310` calls `CL_Disconnect`, which leaves autoreconnect armed. `cl_main.c:665–673` consequently retries timed signon after disconnection. F `cl_parse.c:850–862` explicitly cancels this failure. Reuse `CL_CancelAutoReconnect`, disconnect, loading-plaque cleanup and native menu return at the precache failure boundary. **Current parser polarity requires aborting via `true`**, consumed at `cl_parse.c:3551`; do not copy F’s `false` literally. UI-004 receives this evidence amendment, not a full-row review yet.
- **MOD-011 — P2, two small fixes.** At [sv_phys.c:11749](/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/Quake/sv_phys.c:11749), validate ground *after* Think before returning grounded; reuse F `:6767–6804`, retaining world support and releasing invalid/freed/nonsolid support. At `:3284`, successful legacy elevator nudging needs `SV_LinkEdict(check,false)` before continuing, as F `:4414` and QSS `:922–930` already do.
- **NET-014 — inherited optional smoothing, P3.** F `cl_main.c:1595,1618,1683,1949` produces/decays reconciliation error, and F `view.c:836` actually consumes it. Current source has only reset/storage/diagnostic remnants. Reuse the current command/ACK identities and view preparation boundary for bounded presentation correction; retain discontinuity resets. No new quarantine, protocol or movement authority is needed. Primary default is off; that limits default impact, not source-feature absence.
- **Earlier M findings remain:** NET-007’s late-model tag (`pr_cmds.c:1540`, reuse existing correct helper `:1497`) and loading keepalives (`cl_parse.c:1908,2281`, F `:641,864`); NET-020’s unsupported protocol advertisements (`net_dgrm.c:2672`, trim to actual parser support); NET-021’s initial/retired metadata and SSQC `infokey` fallbacks (`sv_main.c:3578`, `host_cmd.c:2423`, `host.c:646`, `pr_ext.c:4478,4528`; reuse QSS’s existing stores/message boundaries).

No tests, edits, runtime work, branch checks or agents were used. Executable Linux/ARM qualification, malformed/lost/reordered traffic, lifecycle transitions and movement/mod behavior remain delivery gates. User-deferred device, live multiplayer and performance measurements remain separate.

**Lead ledger: 92/185 source-reviewed IDs — 69 S, 6 M, 5 Q, 1 R, 11 X.** The remaining 93 await lead review/integration, including sidecar packets; this is not full185 completion. Accepted audio/GPL3 findings remain carried separately.

Next bounded continuation: **BASE-001..003, PLAT-001..008, and the existing Qs MOD-014, XR-011, PERF-021, PERF-F001**—15 IDs, using main’s briefs and latest dispositions. Input/UI39, AV integration, QC10 and NET-028’s precise contract remain queued.

---

The **40 input/UI IDs are reviewed: 39 S, 1 M**. The M is UI-004’s already identified NET-022 load-failure/reconnect defect; this tranche adds no separate missing-code item. “Support grips” remains covered by inherited model/support-hand geometry, not an invented gripping subsystem.

Paths below are relative to `Quake/`; **P** is primary `51b452c0`. S retains final software qualification.

| ID | Class | Actual owner/evidence |
|---|---|---|
| VR-004 | S | `vr_input.c:4926–4947,5104–5165` accumulates/finalizes roomscale; `view.c:1642–1651` owns units/floor. Reset/reference callers retained. |
| VR-005 | S | `view.c:156–194,308–359` connects mode transitions, authoritative histories and `VR_AimResolve`; `vr_input.c:5074–5096` publishes mapped hand command aim. |
| VR-006 | S | `vr_input.c:422–435,3632–3655,4058–4127` maps physical hands, preserves shared-key ownership and releases/rearms on context/focus changes. |
| VR-007 | S | `vr_input.c:4982–5038` implements once-consumed 180°, signed snap and timed smooth turn through `V_TurnTrackedYaw`. |
| VR-008 | S | `vr_input.c:5040–5072` connects filtered axes, movement basis and finite speed contribution. Instant-stop excluded. |
| VR-009 | S | `vr_input.c:3770–3830,3894–3908,4058` connects profile-specific click/squeeze/axis dispatch and missing-only native bindings. |
| VR-010 | S | `cl_parse.c:1786–1801`, `vr_input.c:438–448`, `vr_openxr.cpp:1395–1400` connect local-sound/menu/wheel pulses to focused physical-hand haptics. |
| VR-011 | S | `vr_input.c:4841–4924` supplies physical muzzle rays; `gl_rmain.c:1315–1377,1508–1592` consumes depth, size, opacity and actual draw geometry. |
| VR-012 | S | `gl_screen.c:1840–1932,2276–2455` connects shared physical panels to native HUD, console, scores, modal/intermission and CSQC drawing. |
| VR-013 | S | `gl_screen.c:1890–1924,2314,2682`, `menu.c:7740–7775` use the same panel/ray for hover and post-draw trigger dispatch. |
| VR-014 | S | `view.c:194–264,308–359,2287`, `cl_parse.c:3573` preserve reset/relative authority and separate physical view from command roll/intermission base. |
| WPN-001 | S | `gl_screen.c:1933–2102,2462–2467,2605–2606`, `gl_rmain.c:2723,2760` connect captured full-basis VR and native desktop wheel presentation. |
| WPN-002 | S | `vr_weapon_menu.c:2928–2960` resolves retained stable hover ID against rebuilt current eligibility, session generation and tracking on release. |
| WPN-003 | S | `vr_weapon_menu.c:589–644,1277–1359`; `sv_main.c:1065–1112` connect explicit descriptors/private masks and active-only discovery. |
| WPN-004 | S | `sv_main.c:945–1030,1106–1112`, `vr_weapon_menu.c:1325–1338,1438–1458` connect validated capacities, reserves/current ammo and readiness. |
| WPN-005 | S | `vr_weapon_menu.c:985–1053` loads stock/profile data; `vr_weapon_calibration.c:2733–2817` reuses contextual/explicit AD presets and authored precedence. |
| WPN-006 | S | `vr_weapon_menu.c:646–675,985–1053` enforces active-path schema/roster loading, complete-roster precedence and reset lifetime. Catalog header is P-identical. |
| WPN-007 | S | `vr_weapon_calibration.c:3005–3050` supplies one finite held calibration; `r_alias.c:889–918` consumes it without a player-count branch. |
| WPN-008 | S | `vr_weapon_calibration.c:1690–1771,2255–2275,3051–3095` connects independent muzzle/global saves, readback/publication and failure rollback. Legacy MP fields are discarded. |
| WPN-009 | S | `view.c:435,570,990–1018`, `r_alias.c:889–918`, `vr_input.c:4732–4824` keep grip/model scale and physical firing muzzle transforms distinct. |
| WPN-011 | S | `vr_weapon_menu.c:2962–3050` rechecks live action, slot/name, session and tracking before fixed native save/load/teleport commands. |
| WPN-012 | S | `gl_model.c:940–1045` generates split geometry; `view.c:1242–1424` prepares pairs; `gl_rmain.c:1384,1441` draws them. `sv_phys.c:4307–4348,4489–4528` and `pr_cmds.c:250,821,1805` consume per-hand ranged poses. |
| MOVE-001 | S | `pmove.c:2358–2510` queries current world/entities and resolves grip/shaft/edge obstruction; `view.c:1330–1369,1508–1575` consumes it. Matches P `vr.c:7388–7478`; no full-mesh damage solver implied. |
| MOVE-003 | S | `vr_input.c:103–127,2083–2120`, `cl_input.c:622`, `r_alias.c:485` connect gesture-only normal attack and observational ready pose. Physical-contact damage publication is disabled. |
| MOVE-008 | S | `view.c:1242–1424`, `vr_input.c:4493–4520` retain per-hand identity/continuity; P-identical `vr_mdl_split.h` feeds `gl_model.c:940`; `r_avatar.c:1987–1997` retains support-hand cosmetic consumer. Parry/hybrids excluded. |
| MOVE-011 | S | `pmove.c:1465,2146`, `cl_main.c:1750–1981`, `sv_phys.c:10389–10746` retain native ladders/liquids/momentum and QC handoff. Gorilla propulsion excluded. |
| FBT-001 | S | P-identical manager; `vr_input.c:1043–1129,1262` connects once-per-sample reconciliation, safe serial/ephemeral identity and explicit role assignment. |
| FBT-002 | S | P-identical filter; `vr_input.c:3189–3268,3275–3359` applies corrected velocity/pose, one filter update per sample and stale expiration. |
| FBT-003 | S | `vr_input.c:2805–2932` connects capture/preview/accept/cancel; `gl_rmain.c:488–522,2627–2649,2734` draws actual calibration targets. |
| FBT-004 | S | P-identical profile/storage; `vr_input.c:737–923,2882–2921`, `menu.c:3135–3204` connect validated save/select/reset and menu role/profile actions. |
| FBT-005 | S | `vr_input.c:3275–3289,3514` makes lower-body targets optional; retained NET-026 codec/capability/relay and `r_vrik_render.c:826,1101` consume them independently. |
| UI-001 | S | `common.c:4038–4161,4246–4301` invokes native store/flavor/root discovery; existing Steam and native Unicode file owners remain. |
| UI-002 | S | `menu.c:4948–5008` retains installed/filter/selection and queues `playgame`; `common.c:3559,4257` uses native game-change/start policy. |
| UI-003 | S | `addon_catalog.c:430,539,562,663,812`, `menu.c:5024–5075` connect validated worker/install/cancel/rename and immutable approved-entry confirmation. |
| UI-004 | **M** | `cl_parse.c:2281–2288` still calls `Host_Error` on a missing precached model; `cl_main.c:332–399,665–681` permits pending retry to survive. Same missing item as NET-022. |
| UI-005 | S | `gl_screen.c:240,744–783`, `pr_cmds.c:146–172`, `menu.c:7740–7775` retain wrapping/localization, relative/manual scales and native pointer handling. |
| UI-006 | S | `vr_openxr.cpp:1280–1282` retains project identity with native engine name; existing Meson/resource/config/version owners remain authoritative. |
| XR-003 | S | `vr_openxr.cpp:456–497,694–717,997,1253,1268,1395–1400` connects per-hand profiles/actions, conditional Frame bindings and focused haptics. |
| XR-008 | S | `vr_openxr.cpp:567–615,932–955,1000,1328–1330` connects HTCX persistent roles/MNDX tracker spaces to the reconciler. Startup-role and preview-ABI limits remain explicit. |
| XR-009 | S | `gl_screen.c:1933–2102,2461–2467,2605–2606` connects wheel draw/hit/model transforms; calibration visuals use the existing native scene owner. |

UI-004’s smallest repair remains the existing load boundary: reuse `CL_CancelAutoReconnect`, disconnect/loading-end/menu handling and the **current caller’s abort return convention**. P `cl_parse.c:850–862` is the behavioral reference; no retry framework.

Lead coverage is now **143/185**: **117 S, 11 M, 2 Q, 2 R, 11 X**. Next is the bounded **12 co-op IDs**, plus explicit AV-001 linkage; the **29 sidecar IDs** await integration. No code or execution occurred.

---

The remaining lead-owned tranche is complete: **COOP-001–006/008–013 and AV-001 are S**. No new missing-code finding emerged.

| ID | Class | Actual owner/evidence |
|---|---|---|
| COOP-001 | S | `host.c:108` finite modern/classic feature policy reaches `world.c:604,631,1929` and `sv_phys.c:6090–6100` consumers. |
| COOP-002 | S | `world.c:604–642,2736,3039,3642` connects telefrag/player-clip policy to trigger, PMove and native collision; `sv_phys.c:197–244,1930–1953` scopes friendly-fire protection around callbacks. |
| COOP-003 | S | `world.c:2206–2247,2626–2669,2740–2797` shares only accepted ownership/progression gains after native touch. Ammo may prove acceptance; it is not team-copied. |
| COOP-004 | S | `world.c:2744–2754,2799–2810`, `sv_phys.c:9373–9404` snapshot unchanged targets, invoke native `SUB_UseTargets` under the existing cancellation scope and clear matching targets. |
| COOP-005 | S | `world.c:884–914,2813–2820` schedules existing `SUB_regen` only for eligible consumed pickups, preserving existing pending thinks and bounded timing. |
| COOP-006 | S | `sv_phys.c:5649–5682,5924–5980,6042–6125` connects safe death/teammate placement, cooldown, inventory and native PostThink completion; `host.c:221,267,602` cancels borrowed policy. |
| COOP-008 | S | `host_cmd.c:3765–3788,3829–3853`, `sv_phys.c:6128–6170,9410` connect authorized relocation, selector cancellation and native safe placement; wheel caller verified separately. |
| COOP-009 | S | `gl_rmain.c:2513,2570–2607,2662–2703,2740`, `r_alias.c:1271` consume prepared body/prop palettes for outlines and depth-tested names. Names are not promised through walls. |
| COOP-010 | S | `host_cmd.c:2677–2706,1949–1996,2090–2159,2214–2217` connects dialect preflight and checked native save publication; `savegame_dialect.h` owns native/inherited discrimination. |
| COOP-011 | S | `host_cmd.c:2500–2530,2560–2598,3587–3674` connects payload-only reserved-edict restore, named pending clients and live/dead/new joins; `sv_phys.c:1383–1404,5981–6021` preserves detached inventory and changelevel behavior. |
| COOP-012 | S | `host.c:908` calls `host_cmd.c:2258–2388`: finite controls, pending-restore refusal, progress/minimum/backoff gates and rotation only after successful native writing. |
| COOP-013 | S | `sv_phys.c:690–855,6042–6080,11403` connects typed QBJ3 recovery/void cleanup to actual PostThink completion and the captured discontinuity epoch. Physical melee and revival are not dependencies. |
| AV-001 | S | Explicitly covered by NET-026: `cl_main.c:106,140,2711`, `sv_user.c:1636–1691`, `sv_main.c:36–70,5365`, `cl_parse.c:313,350` connect selection, capability-gated identity relay, retry and local resolution. |

Lead coverage: **156 unique IDs** — **130 S, 11 M, 2 Q, 2 R, 11 X**. I’m now integrating the **29 completed sidecar rows** and reconciling the draft’s missing-item list. Final totals and the complete crosswalk will follow that integration.

---

**All 185 feature IDs are reviewed and dispositioned: 153 S, 18 M, 2 Q, 11 X, 1 R. No feature IDs remain unreviewed.** This integrates 156 lead-owned rows and 29 completed sidecar rows. It establishes source-review coverage, not executable completion.

The final checklist contains **22 confirmed implementation/delivery subitems and one unresolved transparency design contract shared by two IDs**. No further enumeration continuation is needed. Implementation remains paused.

**S** = source integrated/native, qualification pending; **M** = confirmed missing behavior/delivery; **Q** = exact unresolved source contract; **X** = excluded; **R** = preserved experiment.

Paths below are relative to `Quake/` unless stated otherwise. Pins: **P** primary `51b452c0`; **QSS** `03a498`; **V** vkQuake `4bc898`; **I** Ironwail `08d578`. Earlier `gl_vid.c` receipts mean **`gl_vidsdl.c`**.

The highest-priority functional repairs are tracked avatar root yaw and QBJ3 body/equipment admission, followed by precache/metadata, physics, reconnect cancellation and particle indexing. Packaging omissions block delivery. Smoothing, diagnostic presentation and audio tuning remain required, smaller repairs.

| Item | IDs | Confirmed omission, reference and smallest seam |
|---|---|---|
| C01 | NET-007 | `pr_cmds.c:1522–1540` tags late models as sounds. Reuse correct `SV_Precache_Model` at `:1497`; QSS `pr_cmds.c:1206,1239`. |
| C02 | NET-021 | Initial/full metadata and retired-slot clearing are absent from `sv_main.c:3578`, `host_cmd.c:2423`, `host.c:646`. Reuse signon/spawn/drop messages; QSS `sv_main.c:2276`, `host_cmd.c:9802`, `host.c:1437`. |
| C03 | NET-021 | `pr_ext.c:4478,4528` lacks ordinary SSQC world/player infokey fallback. Reuse existing `Info_GetKey` stores; QSS `:4622,4676`. |
| C04 | NET-007 | Loading keepalive helper is disabled at `cl_parse.c:1908`, with no precache callers at `:2281,2290`. Adapt existing helper preserving parser/message state; P `:641,864,871`. |
| C05 | NET-020 | `net_dgrm.c:2672` advertises unsupported DP7/BJP3. Remove those offer tokens; actual decoder `cl_parse.c:2060` supports NQ/Fitz/RMQ. V inherited the mismatch. |
| C06 | NET-022, UI-004 | Missing required models reach `Host_Error` at `cl_parse.c:2286`; reconnect state survives `cl_main.c:332–399,665–681`. Reuse cancellation, disconnect, loading/menu cleanup; P `cl_parse.c:850–862`. Current caller requires **true** to abort. |
| C07 | NET-014 | `cl_main.c:275` only clears correction state; `:2059` publishes replay directly. Restore bounded optional presentation correction at existing command/ACK/view owners; P `cl_main.c:1595,1618,1683,1949`, `view.c:836`. |
| C08 | MOD-011 | `sv_phys.c:11749` returns grounded after Think without validating support. Adapt P `:6767–6804` ground-reference check immediately before that return. |
| C09 | MOD-011 | Successful legacy elevator nudge at `sv_phys.c:3283` lacks relink. Add existing non-triggering link call; P `:4414`, QSS `:922–930`. |
| C10 | AV-002 | Sampled yaw at `r_alias.c:687` never reaches root construction `:1028`. Carry accepted yaw through existing prepared root/muzzle consumers; P `:5961–5975`. |
| C11 | AV-002 | `r_vrik_render.c:826,1101` lacks viewer-side enable/game eligibility. Restore one preparation predicate while retaining ordinary avatar animation; P `r_alias.c:5809,5961`, `vr.c:3229`. |
| C12 | AV-006 | `r_vrik_render.c:472,476` cannot admit inherited QBJ3 live selection. Adapt exact player-model/143-frame admission and explicit-avatar precedence; P `r_alias.c:5735,5772,5840`. |
| C13 | AV-003, AV-006 | `r_vrik_render.c:465,1250` omits eligible QBJ3 death/queued-corpse selection. Extend existing enumeration with independent corpse palettes and current scoreboard identity; P `r_alias.c:5749,5836`. |
| C14 | AV-006 | `r_vrik_render.c:709,743`, `gl_model.c:6957` provide Ranger equipment only. Adapt verified QBJ3 shotgun/back-wrench resources and optional-equipment fallback; P `r_alias.c:5262,5280,5570`. |
| C15 | AUDIO-011 | Spatial activation `snd_spatial.c:674` bypasses native loop pause `snd_mix.c:444,469`; callback `snd_steamaudio.c:396` advances loops. Project existing pause policy into source activity, preserving cursor/generation. V has native pause behavior. |
| C16 | AUDIO-009 | `snd_spatial.c:679` omits P `:359`’s **0.35** looping-source room-send multiplier. Restore it at the existing assignment. |
| C17 | AUDIO-010 | `voice.c:83` defaults wet monitoring to 1 rather than P `:52`’s **0.6**. Restore initializer only; preserve saved values and independent permission. |
| C18 | PLAT-004; AUDIO-006 delivery | Root `flake.nix:25,49` omits inherited GPL3 notice from source/install closure. Reuse existing packaging for that and applicable component notices; P Linux workflow `:104`, `SPATIAL_AUDIO.md:42,50`. |
| C19 | PLAT-003 | Root `flake.nix:43–49` supplies a store-bound installation, not the retained portable/GLIBC≤2.39 contract. Add architecture-matched portable closure through native packaging and executable-relative loader `vr_openxr.cpp:1213–1235`; P Linux workflow supplies reference. No current binary ABI violation was asserted. |
| C20 | MOD-014 | `gl_screen.c:1100–1136` draws information outside the physical panel transform. Reuse native collector and `gl_draw.c:1331,1344` panel boundary; P `gl_screen.c:748–765`, `gl_draw.c:935–939`. |
| C21 | PERF-F001 | `gl_vidsdl.c:4523–4539`, `gl_rmain.c:2796–2811` lack truthful frame-timing availability/shared-stereo labeling. Preserve native queries/counters; no fabricated per-eye timing or new profiler. |
| C22 | MOD-010 | `r_part.c:163,204,215,238,1021` accepts large pools but wraps 16-bit quad indices. Bound argument parsing/allocation to 512–65,536, restore adopted 32,768 default, and change existing sizing/staging/binding to 32-bit. P `:26,30,153,797`; V contains the defect. |

**Exact remaining design gate:** XR-011/PERF-021 require a minimal correct **non-OIT eye/water-boundary exception**, or proof that existing native/primary consumers already resolve it. Shared sorting alone matches primary and is not M. OIT currently bypasses sorting, but remains optional. Neither a blanket sorter nor forced-OIT/hardware policy is justified.

The complete crosswalk follows. Each S preserves its applicable qualification obligations.

| ID | Class | Actual owner/evidence |
|---|---|---|
| BASE-001 | S | `host.c:1357,1390,1448,1467`; `cl_demo.c:152,203,640,772`: native startup/dedicated/shutdown and bounded desktop demos. |
| BASE-002 | S | `cmd.c:345,403,422,1006–1082`; `host.c:1440`; `common.c:3543`: source-aware dispatch and ordered post-config callers. |
| BASE-003 | S | `cl_input.c:544,568–575`; `in_sdl.c:887–929`: native desktop controls. Primary opposing-key arbitration superseded explicitly. |
| VR-001 | S | `gl_vidsdl.c:4746,4821,5181`: explicit attach/toggle, joined detach, input release and desktop fallback. |
| VR-002 | S | `vr_openxr.cpp:984,1077,1341`; `gl_vidsdl.c:4725`: sampling, session/reference events and retirement. |
| VR-003 | S | `vr_openxr_math.h:52`; `gl_rmain.c:811`; `Shaders/stereo.inc`: independent runtime-eye transforms. |
| VR-004 | S | `vr_input.c:4926–4947,5104–5165`; `view.c:1642–1651`: roomscale finalization, units/floor and reset callers. |
| VR-005 | S | `view.c:156–194,308–359`; `vr_input.c:5074–5096`: resolver/history transitions and mapped command aim. |
| VR-006 | S | `vr_input.c:422–435,3632–3655,4058–4127`: physical identity, shared-key release and context/focus gates. |
| VR-007 | S | `vr_input.c:4982–5038`: once-consumed 180°, signed snap and timed smooth turning. |
| VR-008 | S | `vr_input.c:5040–5072`: filtered axes/basis/finite native speed contribution; instant-stop excluded. |
| VR-009 | S | `vr_input.c:3770–3830,3894–3908,4058`: profile click/squeeze/axis dispatch and missing-only bindings. |
| VR-010 | S | `cl_parse.c:1786–1801`; `vr_input.c:438`; `vr_openxr.cpp:1395`: sound/menu/wheel-to-physical-hand haptics. |
| VR-011 | S | `vr_input.c:4841–4924`; `gl_rmain.c:1315–1377,1508–1592`: physical rays and depth/size/opacity rendering. |
| VR-012 | S | `gl_screen.c:1840–1932,2276–2455`: physical panels feeding HUD/console/scores/modal/intermission/CSQC. |
| VR-013 | S | `gl_screen.c:1890–1924,2314,2682`; `menu.c:7740`: shared ray/panel hover and trigger consumer. |
| VR-014 | S | `view.c:194–264,308–359,2287`; `cl_parse.c:3573`: authoritative continuity and physical/command-angle separation. |
| VR-015 | S | `gl_vidsdl.c:4965,5026,5534,5558,5593`: optional mirror, submission ordering and conservative hidden-area eligibility. |
| VR-016 | S | `gl_vidsdl.c:1525,1558,2749`; `r_passes.c:1152`: native formats/samples and stereo resolve topology. |
| WPN-001 | S | `gl_screen.c:1933–2102,2462,2605`; `gl_rmain.c:2723,2760`: captured VR/native desktop wheel presentation. |
| WPN-002 | S | `vr_weapon_menu.c:2928–2960`: stable hover identity revalidated against live eligibility/session/tracking at release. |
| WPN-003 | S | `vr_weapon_menu.c:589–644,1277–1359`; `sv_main.c:1065–1112`: declarations, masks and active-only discovery. |
| WPN-004 | S | `sv_main.c:945–1030,1112`; `vr_weapon_menu.c:1325,1438–1458`: capacities, current ammo/reserves and readiness. |
| WPN-005 | S | `vr_weapon_menu.c:985–1053`; `vr_weapon_calibration.c:2733–2817`: built-in/contextual profiles and explicit AD preset. |
| WPN-006 | S | `vr_weapon_menu.c:646–675,985–1053`: active-path loading, authoritative roster precedence and reset lifetime. |
| WPN-007 | S | `vr_weapon_calibration.c:3005–3050`; `r_alias.c:889–918`: one finite held calibration for solo/multiplayer. |
| WPN-008 | S | `vr_weapon_calibration.c:1690–1771,2255,3051–3095`: independent muzzle/global persistence, readback and rollback. |
| WPN-009 | S | `view.c:435,570,990`; `r_alias.c:889–918`; `vr_input.c:4732`: distinct grip/model/muzzle transforms. |
| WPN-010 | S | `vr_input.c:4650,4793`; `sv_user.c:733`; `sv_phys.c:4367,4782,4909,8446`: accepted QC firing-source chain. |
| WPN-011 | S | `vr_weapon_menu.c:2962–3050`: live slot/name/action validation before fixed native commands. |
| WPN-012 | S | `gl_model.c:940–1045`; `view.c:1242`; `gl_rmain.c:1441`; `sv_phys.c:4489`; `pr_cmds.c:250,821,1805`: generated pairs and per-hand ranged consumers. |
| COOP-001 | S | `host.c:108`; `world.c:604,631,1929`; `sv_phys.c:6090`: actual modern/classic policy consumers. |
| COOP-002 | S | `world.c:604–642,2736,3039,3642`; `sv_phys.c:197–244,1930`: telefrag/clip and scoped friendly-fire protection. |
| COOP-003 | S | `world.c:2206–2247,2626–2669,2740–2797`: accepted pickup gains and shared progression, without team ammo copying. |
| COOP-004 | S | `world.c:2744,2799–2810`; `sv_phys.c:9373–9404`: unchanged-target snapshot and cancellation-aware native target callback. |
| COOP-005 | S | `world.c:884–914,2813–2820`: eligible native `SUB_regen` scheduling, preserving existing pending thinks. |
| COOP-006 | S | `sv_phys.c:5649,5924–5980,6042–6125`; `host.c:221,267,602`: placement/cooldown/inventory and cancellation. |
| COOP-007 | X | Current user scope excludes revival; ordinary respawn remains COOP-006. |
| COOP-008 | S | `host_cmd.c:3765,3829`; `sv_phys.c:6128–6170,9410`: authorized relocation, safe placement and selector cancellation. |
| COOP-009 | S | `gl_rmain.c:2513,2570,2662,2740`; `r_alias.c:1271`: prepared outlines and depth-tested names. |
| COOP-010 | S | `host_cmd.c:2677–2706,1949,2090,2214`; `savegame_dialect.h`: dialect preflight and checked native publication. |
| COOP-011 | S | `host_cmd.c:2500–2530,2560,3587–3674`; `sv_phys.c:1383,5981`: payload-only restore, named joins and detached inventory. |
| COOP-012 | S | `host.c:908` → `host_cmd.c:2258–2388`: finite controls, progress/backoff gates and success-only rotation. |
| COOP-013 | S | `sv_phys.c:690–855,6042–6080,11403`: typed QBJ3 lifecycle recovery at actual PostThink completion. |
| MOVE-001 | S | `pmove.c:2358–2510`; `view.c:1330,1508`: current-scene grip/shaft/verified-edge obstruction; P `vr.c:7388–7478`. |
| MOVE-002 | X | Gesture-only decision excludes physical reach/contact damage solver. |
| MOVE-003 | S | `vr_input.c:103–127,2083–2120`; `cl_input.c:622`; `r_alias.c:485`: normal-attack gestures and observational ready pose. |
| MOVE-004–007 | X | Same explicit exclusion of physical/contact/hybrid attack adapters; ordinary mod/native play survives. |
| MOVE-008 | S | `view.c:1242–1424`; `vr_input.c:4493`; `gl_model.c:940`; `r_avatar.c:1987`: continuity/support geometry; parry/hybrids excluded. |
| MOVE-009–010 | X | Same explicit exclusion of Gorilla propulsion/contact/platform policy. |
| MOVE-011 | S | `pmove.c:1465,2146`; `cl_main.c:1750–1981`; `sv_phys.c:10389–10746`: native liquids/ladders/momentum and QC handoff. |
| MOVE-012 | X | Gorilla contribution/ACK work excluded; retained wire code adds no obligation. |
| AV-001 | S | `cl_main.c:106,140,2711`; `sv_user.c:1636`; `sv_main.c:36,5365`; `cl_parse.c:313,350`: selected identity/capability/relay/resolution. |
| AV-002 | M | `r_alias.c:687,1028`; `r_vrik_render.c:826,1101`: missing tracked root yaw and viewer/game gate, C10/C11. |
| AV-003 | M | `r_vrik_render.c:465,1250`: QBJ3 death/queued-corpse admission, C13; existing desktop repairs survive. |
| AV-004 | S | `cl_parse.c:350`; `custom_avatar.c:732,752`; `gl_model.c:7749`: key+digest identity and actual admission snapshots. |
| AV-005 | S | `custom_avatar.c:421,501,657`; `gl_model.c:6875,7049,7179,7278,7798`: bounded package/native asset admission and cleanup. |
| AV-006 | M | `r_vrik_render.c:472,476,709,743`; `gl_model.c:6957`: missing QBJ3 selection/equipment/fallback, C12–C14. |
| AV-007 | S | `gl_model.c:132`; `r_vrik_render.c:1323`; `r_alias.c:411`; `gl_mesh.c:1817`; `r_brush.c:3188`: independent shared palettes/resources. |
| AV-008 | S | `r_vrik_render.c:490,905,1116`; `r_avatar.c:747,786,897`: calibrated custom-rig policy and tracked targets. |
| AV-009 | R | P `r_alicia_spike.c:70,80,261`: fingerprinted OpenGL/CPU experiment, not a general production VRM importer. |
| FBT-001 | S | P-identical manager; `vr_input.c:1043–1129,1262`: once-per-sample identities and explicit roles. |
| FBT-002 | S | P-identical filter; `vr_input.c:3189–3268,3275–3359`: correction, prediction and stale expiration. |
| FBT-003 | S | `vr_input.c:2805–2932`; `gl_rmain.c:488,2627,2734`: capture/preview/accept/cancel and visible targets. |
| FBT-004 | S | P-identical profile/storage; `vr_input.c:737–923`; `menu.c:3135–3204`: save/select/reset and role/profile UI. |
| FBT-005 | S | `vr_input.c:3275,3514`; NET-026 codec/relay; `r_vrik_render.c:826,1101`: optional independently negotiated lower-body targets. |
| MOD-001 | S | `pr_ext.c:6494,6790,6811,6860,7002`; `pr_edict.c:2072,2296`: numbered/VM-specific dispatch and loader remaps. |
| MOD-002 | S | `cl_main.c:2734`; `sbar.c:895,903`; `gl_screen.c:2326,2646,2670`: command filtering, HUD clock and GUI/error ownership. |
| MOD-003 | S | `pr_ext.c:5439,5523,5552,6314`; `gl_draw.c:856`: client drawing, panel clipping, padded subpictures and alpha. |
| MOD-004 | S | `pr_ext.c:3286,3433,3608,3677,3806,4039,4077,4110,6916`; `common.c:2806`: file/search/buffer/string lifetimes. |
| MOD-005 | S | `pr_ext.c:4145,1988,2291,4352,4374,4417`; `pr_edict.c:1301,1917`; `pr_cmds.c:1413,1774`: entity/surface/reflection consumers. |
| MOD-006 | S | `pr_cmds.c:2153,149,2004`; `pr_edict.c:2054`; `common.c:2446,4929,5154,5213,5393`: EX/localization consumers. |
| MOD-007 | X | Skyrooms explicitly excluded; retained server-side source does not reverse that decision. |
| MOD-008 | S | `pr_cmds.c:2215`; `sv_main.c:2586`; `cl_parse.c:1285`; `r_alias.c:1220`; `cl_main.c:2371,2479`: static/alpha/trail consumers. |
| MOD-009 | S | `r_part_fte.c:937,1142,3609,3636,6693`; `gl_rmain.c:2963,2984,3010`: effect loading/lifetimes and task ordering. |
| MOD-010 | M | `r_part.c:163,204,215,238,1021`: large-pool quad-index wrap, C22; weather/palette/beam owners remain. |
| MOD-011 | M | `sv_phys.c:3283,11749`: C08/C09; native customphysics, ladder, velocity and robust-pusher consumers otherwise present. |
| MOD-012 | S | `pr_edict.c:80,1411,1835`; `host_cmd.c:3044,3052,3072,3196`; `sv_phys.c:11565`: saved references/FIFO/load teardown. |
| MOD-013 | S | `cl_input.c:382,628,1115`; `sv_user.c:1330`; `menu.c:4454,4569`; `common.c:3477,3545`: impulses/bindings/switching. |
| MOD-014 | M | `gl_rmain.c:1884`; `gl_screen.c:1100–1136`: collector exists, physical-panel placement missing, C20. |
| UI-001 | S | `common.c:4038–4161,4246–4301`; native Steam/platform file owners: store/flavor/root and Unicode discovery. |
| UI-002 | S | `menu.c:4948–5008`; `common.c:3559,4257`: installed/filter/selection and native `playgame` launch. |
| UI-003 | S | `addon_catalog.c:430,539,562,663,812`; `menu.c:5024–5075`: validated worker/install/cancel and approved-entry confirmation. |
| UI-004 | M | `cl_parse.c:2286`; `cl_main.c:332–399,665–681`: same missing-model retry cancellation as C06/NET-022. |
| UI-005 | S | `gl_screen.c:240,744–783`; `pr_cmds.c:146–172`; `menu.c:7740`: wrapping/localization/scaling/native pointer. |
| UI-006 | S | `vr_openxr.cpp:1280–1282`; native Meson/resource/config/version owners: stable project/native engine identities. |
| AUDIO-001 | S | `voice.c:233,1053,1299`; `cl_input.c:952,1166,1263`; `sv_main.c:308,745`: capture/encode/bounded relay/decode chain. |
| AUDIO-002 | S | `voice_settings.c:126,174`; `voice.c:501,519,1040,1073,1615`: saved profiles, opt-out and transmit/PTT gates. |
| AUDIO-003 | S | `voice.c:384,488,519,762,787`: default-system/unique explicit capture, failure cooldown, retry and revocation. |
| AUDIO-004 | S | `voice_jitter.c:134,202`; `voice.c:933,952,1387,1559`; `cl_main.c:295,351`: jitter/generations/mute/gain/reset. |
| AUDIO-005 | S | `host.c:1277`; `snd_dma.c:916`; `voice.c:1072,1111,1599`; `sbar.c:1332,1401`: once-frame update and HUD consumers. |
| AUDIO-006 | S | `snd_dma.c:224,264,388`; `snd_spatial.c:428,635`; `snd_sdl.c:45`, `snd_sdl3.c:43`: spatial/fallback ownership; delivery C18. |
| AUDIO-007 | S | `snd_spatial.c:680`; `voice.c:1434`; `snd_steamaudio.c:421`: calibrated muzzle and positional/radio blend consumers. |
| AUDIO-008 | S | `snd_spatial.c:138,179`; `snd_steamaudio.c:99,438`: static-world obstruction and filter/compression/drive controls. |
| AUDIO-009 | M | `snd_room.c:63,223`; `snd_spatial.c:679,957,981`: worker lifecycle exists; loop room-send C16 missing. |
| AUDIO-010 | M | `voice.c:83,549,618,1065,1429`: wet-only permission/output present; adopted default restoration C17. |
| AUDIO-011 | M | `snd_mix.c:444,469`; `snd_spatial.c:674`; `snd_steamaudio.c:396`: loop-pause C15; native music/format owners survive. |
| XR-001 | S | `vr_openxr.cpp:1238,1490,1534,1638`; `gl_vidsdl.c:1339,2379`: runtime GPU/creation metadata and native adoption. |
| XR-002 | S | `vr_openxr.cpp:1013,1028,1039,1077,1341`; `gl_vidsdl.c:5026`: bounded image ownership/events/submission. |
| XR-003 | S | `vr_openxr.cpp:456–497,694–717,997,1253,1268,1395`: profiles/actions, conditional Frame bindings and haptics. |
| XR-004 | S | `vr_openxr.cpp:668,957,1299`; `gl_vidsdl.c:5243`; `vr_foveation_policy.h:31`: optional finite/fresh tracked gaze. |
| XR-005 | S | `gl_vidsdl.c:5091,5118`; `glquake.h:888`; `r_brush.c:1106`; `r_world.c:1452`: rate-map production/material admission. |
| XR-006 | S | `vr_openxr.cpp:1749`; `gl_vidsdl.c:5046`; `gl_rmain.c:2274`; `r_passes.c:1152`: paired centers/fine-depth/protected resolve. |
| XR-007 | S | `view.c:89`; `vr_foveation_policy.h:15,31`; `vr_openxr.cpp:1749`: eye default, explicit fixed mode and full-rate fallback. |
| XR-008 | S | `vr_openxr.cpp:567–615,932–955,1000,1328`: HTCX persistent roles/MNDX tracker spaces; stated ABI/startup limits. |
| XR-009 | S | `gl_screen.c:1933–2102,2461–2467,2605`; existing calibration scene owner: shared draw/hit/model transforms. |
| XR-010 | S | `gl_rmain.c:811,2848`; `r_world.c:994`; `Shaders/indirect.comp:88`: shared preparation and either-eye visibility. |
| XR-011 | Q | `gl_rmain.c:2345–2465`; `gl_rmisc.c:114`; `gl_vidsdl.c:4677,5205`: exact non-OIT eye/water-boundary contract. |
| XR-012 | S | `gl_rmisc.c:3197,3242,3337,3425,3482,5072`: bounded persisted pipeline cache and actual native consumers. |
| PLAT-001 | S | Root `meson.build:340–424,531,560–568`, `flake.nix:8,14,28–42`: platform sources/dependencies/install recipes. |
| PLAT-002 | S | `vr_openxr.cpp:1238,1490,1534,1638`; `gl_vidsdl.c:4746,5026,5181`: common target-runtime/device/stereo ownership. |
| PLAT-003 | M | Root `flake.nix:43–49`; `vr_openxr.cpp:1213–1235`: portable architecture/ABI/loader delivery C19. |
| PLAT-004 | M | Root `flake.nix:25,49`, license inventory; P GPL3/workflow artifacts: inherited notice delivery C18. |
| PLAT-005 | S | Root `meson.build:113–142,352,560–568`, `flake.nix:47–48`: generated/consumed PAK and native installation assets. |
| PLAT-006 | S | Root `flake.nix:18–26`: explicit public source closure; private deployment/telemetry outside product packaging. |
| PLAT-007 | S | Existing focused test sources and pinned-reference ledgers remain inputs; actual final Linux/ARM execution is a delivery gate. |
| PLAT-008 | S | Preservation ledger’s 12 WIP hashes and upstream/reference records retained; WIP content remains reference-only; merge rehearsal pending. |
| PERF-F001 | M | `gl_vidsdl.c:4456–4539`; `gl_rmain.c:2796–2811`: native timing owners, missing truthful presentation C21. |
| PERF-F002 | S | `gl_model.c:2458,2514,2640,2694,2748,3289`: initialized styles, bounded references and exact-count plane storage. |
| PERF-F003 | S | `r_brush.c:3109`; `gl_mesh.c:1622,1746`; `gl_rmain.c:2887`: palette dependencies and animated BLAS/TLAS consumers. |
| NET-001 | S | `sv_main.c:2889,3394,3456,3672`; `cl_parse.c:2014,2135`: protocol selection/intersection/limits/validation. |
| NET-002 | S | `protocol.h:461`; `sv_main.c:3421`; `cl_parse.c:2027,2075`; `sv_user.c:2108`: explicit private profile. |
| NET-003 | S | `sv_main.c:1914,2256,2529,2669,2751,2803,4605`: recipient deltas/customization/solid/owner seeding. |
| NET-004 | S | `sv_main.c:1590,1656,2201,4702–4767`; `cl_parse.c:1376,1485`; `cl_input.c:812`: ACK/resend/continuation ownership. |
| NET-005 | S | `common.c:1319,1560`; `sv_main.c:1427`; `cl_parse.c:810,1033`; `pmove.c:2550`: precision/solid encoding consumers. |
| NET-006 | S | `sv_main.c:1059,1094,1772,1819`; `cl_parse.c:1700,3101,3849`; `pmove.c:2704`: typed stats/movement variables. |
| NET-007 | M | `pr_cmds.c:1497,1522`; `cl_parse.c:1908,2281,2977`: late-model tag and loading keepalive C01/C04. |
| NET-008 | S | `cl_parse.c:1079,3565,3894`; `view.c:264`: native effect consumption, authoritative angles and public VM event gates. |
| NET-009 | S | `host.c:1064,1190`; `cl_main.c:2680,2765`; `sv_main.c:1610,2368,4725`; `cl_parse.c:2327–2438`: CSQC entity lifecycle. |
| NET-010 | S | `cl_input.c:1100,1152`; `sv_user.c:1199,1232,1440,1572`; `server.h:179`: bounded redundant command sequencing. |
| NET-011 | S | `cl_input.c:843`; `sv_user.c:657,1202`; `sv_phys.c:9899,10755`: finite monotonic msec and authority credit. |
| NET-012 | S | `host.c:156,187,1200,1235`; `cl_main.c:2664`: native render/physics cadence and once-render private command production. |
| NET-013 | S | `cl_main.c:1750–1981,2143`; `sv_phys.c:10389,10546,10618,10746`: replay, QC velocity and handoff/timer consumers. |
| NET-014 | M | `cl_main.c:275,2059`: correction storage/reset without producer/view consumer; optional smoothing C07. |
| NET-015 | S | `cl_input.c:657,732`; `sv_user.c:794,1080,1940,2022`; `pr_ext.c:5815,6341`: private inputs and framed QC events. |
| NET-016 | S | `net_dgrm.c:405,453,485,566,818`; `net_main.c:203`: fragmentation/retry/ACK/EOM and MSS bounds. |
| NET-017 | S | `net_dgrm.c:111–195,770,799,1811`; NAT review `:23`: validated remap; fast same-IP pruning explicitly rejected. |
| NET-018 | S | `net_loop.c:33,71,136,184,217,268`; `net_main.c:163`: native loopback sequence/send/reset ownership. |
| NET-019 | S | `net_bsd.c`, `net_win.c`, `net_udp.c`, `net_wins.c` match V; native driver IPv4/IPv6 dispatch retained. |
| NET-020 | M | `net_dgrm.c:2672`; `cl_parse.c:2060`: unsupported advertised dialects C05; native discovery/control remains. |
| NET-021 | M | `sv_main.c:3578`; `host_cmd.c:2423`; `host.c:646`; `pr_ext.c:4478,4528`: metadata/infokey C02/C03. |
| NET-022 | M | `cl_main.c:448,543,665,837–958`; `cl_parse.c:2286`: installer/reconnect owners present; terminal model failure C06 missing. |
| NET-023 | S | `vr_input.c:5129`; `cl_input.c:703`; `sv_user.c:733,1486`; `sv_phys.c:3763,4367`: finite roomscale/private pose chain. |
| NET-024 | S | `vr_input.c:4487,5159`; `cl_input.c:716`; `sv_user.c:760`; `sv_phys.c:4310,4401`: surviving paired-pose transport. |
| NET-025 | X | Gorilla-specific transport/authority excluded; existing validation remains. |
| NET-026 | S | `cl_input.c:881`; `sv_user.c:1715,1877,2125`; `sv_main.c:540,590,625,4856`; `cl_parse.c:474,588`: negotiated pose relay/consumers. |
| NET-027 | S | `cl_parse.c:200,263,711`; `sv_user.c:1811,1848`; `sv_main.c:258,745,3644,4866`: framed/generation-aware gameplay-first voice relay. |
| NET-028 | S | `cl_main.c:3224`; `host_cmd.c:1656`; `sv_main.c:863–897`: native ordinary solo and explicit `qsvr1` shared-command opt-in. |
| NET-029 | S | `sv_phys.c:3770,11403,11422,11562`; `world.c:565,2734,2778`; `pr_edict.c:169`: teleport/death/discontinuity lifecycle. |
| PERF-001 | S | `tasks.c:296,453`: actual scalar/indexed scheduling and worker creation. |
| PERF-002 | S | `gl_rmain.c:2848`: submitted renderer/particle/lightmap/BLAS dependency graph and retained serial path. |
| PERF-003 | S | `gl_model.c:1525,1847`: native texture jobs and join before return. |
| PERF-004 | S | `gl_model.c:4470,6609,6760`: MDL/MDX skin jobs, distinct slots and joined temporary lifetimes. |
| PERF-005 | S | `gl_model.c:2595`: indexed extents jobs with worker-context serial fallback. |
| PERF-006 | S | `mem.c:88,120`; native loader callers: dynamic allocation rather than replacement fixed heap. |
| PERF-007 | S | `gl_model.h:795`; `gl_model.c:2973`: indexed marksurfaces consumed by native visibility. |
| PERF-008 | S | `r_brush.c:2795`: uploaded ordinary polygons released; tiled/BSP consumers retained. |
| PERF-009 | S | `r_brush.c:1994,1310`: native sorter/packing; sorter matches V `:1799`. |
| PERF-010 | S | `gl_model.c:3716,3742`: BSP29/2PSB/BSP2/Valve/Quake64 loader dispatch. |
| PERF-011 | S | `mem.c:88,120` and native loader allocation; named-map load/exit remains qualification, not proven by allocation alone. |
| PERF-012 | S | `gl_model.c:1847,2595,4470,6760`: actual parallel loading/join mechanisms; no measured speedup claimed. |
| PERF-013 | S | `r_world.c:994`; stereo frusta; `Shaders/indirect.comp:88`: gaze-independent conservative visibility. |
| PERF-014 | S | `r_brush.c:1053`; `r_world.c:1234`: native direct/indirect material/atlas/alpha boundaries. |
| PERF-015 | S | `r_brush.c:3912`; `Shaders/update_lightmap.inc:289`: dirty-lightmap work and either-eye lighting admission. |
| PERF-016 | S | `r_world.c:994`: native no-VIS/PVS and serial/worker paths; no claim of equal cost to primary GL cache. |
| PERF-017 | S | `r_alias.c:91,110,135,397`; `gl_rmain.c:1216,1275`: bounded compatible instance batching and immediate exclusions. |
| PERF-018–019 | S | `gl_vidsdl.c:1525,2749`: same native high-precision color/depth selection and stereo attachment consumers. |
| PERF-020 | S | `Shaders/world.vert:45`; `r_brush.c:1152`: masked reversed-Z bias matching I `gl_shaders.h:518`. |
| PERF-021 | Q | Same `gl_rmain.c:2345–2465`/OIT consumers and exact non-OIT water-boundary contract as XR-011. |
| PERF-022 | S | `r_alias.c:1157`: loaded offscreen aliases rejected before skin/pose work; named-map correctness remains pending. |
| PERF-023 | S | `gl_mesh.c:1491,1662,1767`: exact untracked pose-cache reuse; tracked palettes bypass cache. |
| ASSET-001 | S | `image.c:157`: path-priority-first image selection and native decoder dispatch. |
| ASSET-002 | S | `gl_model.c:6609,8059`: MD3 fallback names feed native skin/material workers. |
| ASSET-003 | S | `gl_model.c:7236`: MD5 loading through existing model/material ownership. |
| ASSET-004 | S | `r_alias.c:391`; native skin descriptors: glow/luma/indexed fullbright consumers. |
| ASSET-005 | S | `gl_model.c:1525`: map/global external-texture search paths and actual loading. |
| ASSET-006 | S | `gl_model.c:1540` and alias loaders: search-path precedence floor retained. |
| ASSET-007 | S | `gl_model.c:1228,1280,1706,1741`: WAD lists/palettes and bounded loader callers. |
| ASSET-008 | S | `gl_model.c:2539`; `r_world.c:1395`; `r_brush.c:1156`: lit-liquid samples/atlas consumers. |
| ASSET-009 | S | `r_light.c:41`; `r_brush.c:1231,3912`: native CPU/GPU lighting-mode consumers. |

The final dispositions explicitly preserve these boundaries:

- **BASE-003:** native opposing-key subtraction governs; do not restore `cl_iDrive` arbitration or blanket aliases.
- **NET-017:** native established timeout is deliberate policy, not equivalent fast pruning. Correct stale acceptance wording; add no same-IP eviction.
- **NET-028:** explicit `qsvr1` solo opt-in may use the shared private command path. No demonstrated default-play regression warrants another guard.
- **PLAT-008:** S covers preservation/integration ownership. Preserved WIP does not become production implementation.
- **AV-009:** retain the experiment and conditional asset/performance discussion. No general importer or mandatory manual-animation conversion follows.
- **PERF-F001:** truthful shared/availability reporting is required; per-eye numbers must not be fabricated.

The interface/history obligations remain attached to the checklist: **512 QC rows, 1,303 command/settings rows, 906 preservation paths, 540 history entries—497 MAIN and 43 XR**. The absent-literal screen’s 285 declarations/145 unique pairs is not a missing-feature count. Current receipts resolve native equivalents, unsupported declarations, retired settings, exclusions and experiments. Registry names and routing counts do not certify every signature, permission or executed VM contract.

After implementation, retain all eight software/delivery gates:

1. Complete Linux x86-64 client/dedicated and isolated native ARM64 client builds with required OpenXR/shaders/codecs/CURL/Steam Audio dependencies.
2. Native desktop campaigns, mission packs, controls, graphics/AO, demos, configuration ordering and lifecycle.
3. Actual two-eye rendering, visibility, transparency/water, MSAA, panels, wheel, outlines and body/prop/muzzle/shadow agreement.
4. Real public/private/native network paths, prediction, loss/reordering, metadata, voice, slot reuse and mod-install/reconnect cancellation.
5. Loaded QC contracts, resource lifetimes, malformed-input cleanup, co-op/save/hub identity and callback behavior.
6. Required representative large/mod maps, including mj4m1, with correct load/render/exit and no heapsize workaround.
7. Portable installed resources, architecture-matched dependencies, loader resolution, GLIBC ceiling/notices and one disposable substantive upstream merge rehearsal.
8. Close resulting software findings and perform final local Astra integration review.

Excluded remain revival, skyrooms, quad views, Gorilla/hand-swim/instant-stop, physical-contact melee/parry/hybrids, Mjolnir dual-state extensions, imagedump, VR/additional demos, legacy aliases and general incompatible-device reconstruction. Windows builds and user headset/gaze/listening/live-multiplayer/performance trials remain deferred.

The eleven mapped additions—background saves, general connection retries, demo timeline, dithering, live previews, per-file downloads, CSQC prediction APIs, DP7/BJP3, clustered lighting, ICE/WebRTC and 255 slots—remain optional research; demo timeline is additionally excluded.

Verified local Astra xhigh provenance applies to this source review. No edits, builds, tests, runtime actions, branch checks, telemetry access or nested agents occurred.

---

**Final source-review totals: 185 reviewed — 153 S, 18 M, 2 Q, 11 X, 1 R.** No unreviewed feature IDs remain. PLAT-008 is **S**; its preserved WIP material remains reference-only. AV-009 is the sole **R**.

These tables supplement the retained per-ID returns. Paths are relative to `Quake/` unless marked otherwise; **P** = primary `51b452c0`. S means source integrated, qualification pending.

| ID | Class | Actual owner/evidence and disposition |
|---|---|---|
| BASE-001 | S | `host.c:1357,1390,1448,1467` connects native initialization, dedicated startup and shutdown. `cl_demo.c:152,203,640,772` retains bounded desktop recording/playback/seek; `sbar.c:589,633,662` retains mission-pack branches. |
| BASE-002 | S | `cmd.c:1006–1082` preserves source-aware native/CSQC dispatch. `cmd.c:345,403,422,427`, `host.c:1440–1452`, `common.c:3543–3547` connect ordered post-config loading to startup and game changes. |
| BASE-003 | S | `cl_input.c:544,568–575`, `in_sdl.c:887–929` retain native desktop movement, mouse, joystick and gyro consumers. The explicit native desktop contract supersedes P’s later-opposing-key `cl_iDrive` behavior. |
| PLAT-001 | S | Root `meson.build:340–424,531,560–568` selects platform sources/dependencies/install owners; `flake.nix:8,14,28–42` supplies Linux x86/ARM recipes. Builds remain unqualified. |
| PLAT-002 | S | `vr_openxr.cpp:1238,1490,1534,1638`, `gl_vidsdl.c:4746,5026,5181` connect runtime/device discovery, native Vulkan adoption and stereo submission. Target-device qualification remains separate. |
| PLAT-003 | **M** | Root `flake.nix:43–49` produces a store-bound installation, including an absolute loader link. Portable architecture-matched closure and GLIBC≤2.39 delivery remain missing. Reuse native packaging and executable-relative loader search at `vr_openxr.cpp:1213–1235`; P Linux workflow supplies the portability/ABI reference. |
| PLAT-004 | **M** | Root `flake.nix:25,49` and current license inventory omit the inherited GPL3 notice from source/install closure. Reuse existing packaging for P’s `LICENSE-GPL-3.0.txt` and applicable component notices; P Linux workflow `:104`, `SPATIAL_AUDIO.md:42,50`. |
| PLAT-005 | S | Root `meson.build:113–142,352,560–568` generates, consumes and installs native embedded resources; `flake.nix:47–48` installs desktop/icon assets. Portable closure remains PLAT-003. |
| PLAT-006 | S | Root `flake.nix:18–26` uses an explicit engine/build-resource source set. Private deployment material remains outside product packaging; final artifact inspection is pending. |
| PLAT-007 | S | Existing focused production-owner test sources and pinned-reference ledgers remain qualification inputs. Their presence does not establish execution or parity; consolidated meaningful Linux/ARM checks remain required. |
| PLAT-008 | S | `docs/migration-preservation.csv` retains the 12 nonempty `wip_sha256` records; `docs/final-scope-interface-history-2.0.md:36–54` reconciles WIP/research dispositions. Archival reconciliation and merge ownership survive; experimental contents are reference-only. Disposable substantive upstream merge rehearsal remains a delivery gate. |

The required classification revisions are:

| ID | Final class | Evidence and exact disposition |
|---|---|---|
| MOD-014 | **M** | `gl_screen.c:1100–1136` draws the existing collector outside a tracked panel. Reuse `gl_draw.c:1331,1344` at that canvas boundary; P `gl_screen.c:748–765`, `gl_draw.c:935–939`. No new collector. |
| PERF-F001 | **M** | `gl_vidsdl.c:4523–4539` owns native frame/AO timing; `gl_rmain.c:2796–2811` lacks truthful frame availability/shared-stereo presentation. Preserve queries/counters; no fabricated per-eye split. |
| XR-011 | **Q** | `gl_rmain.c:2345–2465` uses shared sorting and center-leaf water partition; `gl_rmisc.c:114` bypasses sorting under OIT, but `gl_vidsdl.c:4677,5205–5208` permits OIT off. Resolve only the non-OIT eye/water-boundary exception or prove existing consumers suffice. |
| PERF-021 | **Q** | Same actual consumers and single shared design gate as XR-011. Primary shared sorting is intentional; no blanket sorter or forced-OIT policy. |
| NET-028 | **S** | `cl_main.c:3224` keeps ordinary solo native; `host_cmd.c:1656` exposes explicit `qsvr1`; `sv_main.c:863–897` gates shared-command admission. The accepted opt-in contract does not require primary’s solo exclusion. |
| AUDIO-010 | **M** | Main-adopted restoration: `voice.c:83` default 1 versus P `voice.c:52` default 0.6, consumed at current `:1429`. Change only the initializer; preserve saved values and independent permission. |

The **22 confirmed subitems and one shared transparency design gate** remain the final implementation scope. Preserve the eight end-of-implementation software/delivery groups and the explicit exclusions. Keep missing implementation, pending qualification and optional research distinct.

No additional source tranche is needed. After main merges these rows into the canonical CSV, the remaining review is a **brief synthesis/consistency check only**, using the retained evidence. No edits or execution occurred.