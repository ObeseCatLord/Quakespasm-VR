# Interface and history evidence for the final scope audit

2026-10-01, production snapshot `2b420380`. Main-thread source screening for
the full senior enumeration. This is supplementary evidence, not a final
missing-feature list or executable qualification. The user-owned dirty
`migration-2.0.md` was not used as acceptance evidence or edited.

| Inventory | Observed scope | What it establishes |
| --- | --- | --- |
| Feature map | 185 unique rows, 14 categories | Review coverage units; overlapping rows are not a completion percentage. |
| QC interfaces | 512 rows, 256 per MAIN/XR pin | Literal registry coverage. Current native core/extensions contain every inventoried quoted name; this alone proves neither slot, permission, signature, implementation nor capability agreement. |
| Command/settings interfaces | 1303 rows | Literal declarations. Current flat Quake C/C++/header screening lacks 285 declarations corresponding to 145 unique kind/name pairs. Conditional declarations, native equivalents and retired settings must be reconciled before identifying gaps. |
| Preservation | 906 paths | 709 feature-routed paths, 184 historical deletions, 12 preserved WIP paths and one later primary document. No empty feature routes. Routes and hashes preserve evidence; they do not prove behavioral integration. |
| History | 540 rows: 497 MAIN, 43 XR | Surviving behavior, not replaying every commit or restoring reverted work, governs acceptance. No empty feature routes. Later primary updates are separately reconciled in migration-source-updates.md. |

Older summary counts of 905 preservation paths and 539 history rows predate
the `51b452c0` wheel update. The schema and historical pins remain valid.
The full senior crosswalk must resolve surviving behavior and supplemental
contracts; counting routes is not a whole-history parity claim.

## Source dispositions and candidates supplied to the reviewer

| Observation | Actual evidence and proposed disposition |
| --- | --- |
| Missing old command names | [Command source checkpoint](migration-command-source-checkpoint.md) records current registrations/consumers, native equivalents, waived legacy setting migrations, unified calibration, retired allocator/GL controls, optional experiments and excluded imagedump. Preserve behavior rather than import aliases or another owner. |
| Same-IP stale-slot pruning | Current net_dgrm.c:111–195 implements unambiguous validated rebinding and previous-endpoint protection. The [NAT review](migration-nat-demux-review.md) explicitly rejects the inherited three-second same-IP eviction because legitimate quiet peers can be disconnected. Keep native established timeout. The old NET-017 description is historical reference evidence, not an instruction to restore rejected eviction. |
| Opposing keyboard directions (`cl_iDrive`) | Primary cl_input.c:418–431 chooses the later pressed opposing key. Current cl_input.c:568–569 subtracts native key states. This is a concrete source difference, but the user also requests native vkQuake desktop behavior. Senior disposition must explicitly reconcile those contracts; missing spelling alone is insufficient. |
| `cl_mwheelpitch` | Primary whole-Quake search finds declaration/registration but no behavioral consumer. Do not invent missing wheel-pitch behavior from that declaration. |
| `cl_mousemenu` | Primary in_sdl.c/menu.c uses it to disable desktop hover. Current native menu.c:7755–7767 already owns desktop/VR pointer updates. Native mouse menus are present; preserving the old toggle is not required by the legacy-settings waiver. |
| `pm_watersinkspeed` / `pm_flyfriction` | Current pmove.c:2674–2675 consumes info defaults, :2756–2757 consumes movement stats, and :2869/:2872 supplies server defaults 4/60. Native movement-variable producers/consumers must agree; absent old settings do not establish missing movement. |
| `r_bloodstains` | Primary r_part_fte.c:456–458 places the setting under `#if UNSUPPORTED`. Current scripted particles retain decal paths. Do not classify an unsupported reference path as a supported-product regression. |
| Spatial room/radio settings | [Spatial source checkpoint](migration-spatial-audio-review.md) identifies current settings projection and actual room/filter/compression/drive/reverb consumers. No second DSP owner or legacy setting-name compatibility is required. Native ARM SDK delivery remains a separate software/delivery obligation. |
| Renderer settings and experiments | [Renderer source checkpoint](renderer-source-checkpoint-2.0.md) identifies native task/load/allocation/PVS/batching/lightmap/precision owners and incremental stereo paths. Primary OpenGL GPU-worldmark cache, allocator controls and GL device/sampler options do not require parallel Vulkan systems. Native rendered correctness remains unqualified. |
| Alicia/VRM experiment | [Avatar checkpoint](custom-avatar-source-checkpoint-2.0.md) distinguishes integrated palette/IK ownership from AV-009's preserved, default-off fingerprinted OpenGL comparison. General VRM release support is not established by the experiment. Reconcile the user's performance preference without inventing a second production importer. |

## Preserved WIP and optional research

The preserved WIP rows cover PMove; brush/lightmap/Vulkan renderer experiments;
VR input; Gorilla; world declarations; README; Gorilla/Vulkan-lighting docs;
and player-model documentation. Their immutable evidence stays local. Existing
native networking/renderer/avatar owners and current scope decisions govern
integration. Excluded Gorilla/swim propulsion and superseded experimental
renderer code do not become required merely because a WIP hash was retained.
Historical deletions require their documented context, not automatic restoration.

[Useful additions](migration-useful-additions.md) maps eleven candidates:
background saves, connection retries, demo timeline (user-excluded), dithering,
live previews, per-file downloads, CSQC prediction APIs, DP7/BJP3, clustered
lighting, ICE/WebRTC and 255 slots. Mapping was requested; blanket implementation
was not. Native desktop demos remain required. Requested performance mechanisms,
asset formats, QSS-M style predictive networking and current OpenXR behavior
remain required independently of these proposals.

All production and final qualification questions belong in the reviewed final
crosswalk/checklist. No test, build, compiler, lint, probe, fixture, benchmark,
game, microphone or deployed-server action ran for this screening.
