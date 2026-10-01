# Renderer and large-map source checkpoint

2026-09-30 source inspection on2.0. This reconciles inherited inventory labels
with actual owners; it is not a new renderer plan or a software/performance pass.
Use vkQuake `4bc898f2` as native behavior, Ironwail `08d57813` for the specific
adapters already recorded in [renderer map](migration-renderer-map.md). Keep
vkQuake graphics. No measurements are required by the active user scope.

| Requirements | Actual source evidence | Remaining proof |
| --- | --- | --- |
| PERF-001 worker scheduling | tasks.c:296 executes queued scalar/indexed tasks; :453 creates the native worker threads. | Final software scheduling/shutdown checks. |
| PERF-002 multithreaded rendering | gl_rmain.c:2832 constructs the existing task graph and submits its native render/particle/lightmap/BLAS jobs. Stereo extends the existing contexts. | Final desktop/stereo/software correctness. |
| PERF-003–005 parallel texture/skin/BSP loading | gl_model.c:1847,2586,4455,6751 submit indexed native loaders then join before their owning temporary inputs leave scope. Worker callers retain serial paths. | Final representative map/assets loading and cleanup. |
| PERF-006 dynamic allocation | mem.c:88/117 use native selected calloc/realloc; brush/model loaders use these owners rather than requiring a fixed Quake hunk. | Named large maps must still load without heapsize; allocator presence alone is not that proof. |
| PERF-007 compact marksurfaces | gl_model.h:795 stores integer indices; r_world.c:532–537 consumes them against owning world surfaces. | Final visible-scene/map coverage. |
| PERF-008 ordinary CPU polygon release | r_brush.c:2795–2820 releases uploaded ordinary polygons, retaining tiled users; :2680 validates BSP reconstruction sources. | Final reload, decals/showtris and tiled correctness. No RSS claim. |
| PERF-010 widened BSP formats | gl_model.c:3719–3733 dispatches BSP29/2PSB/BSP2/Valve/Quake64 through the native loader. | Final large-map loads including named maps. |
| PERF-013 conservative stereo culling | r_world.c:997–1032 unions center/either-eye PVS and water-portal rules; gl_rmain.c:873–933 handles conservative stereo frusta; Shaders/indirect.comp:88–117 rejects a face only when both transformed eye-plane tests permit it. | Final visibility at eye/PVS/water/moving-brush boundaries. |
| PERF-017 alias batching | r_alias.c:110–150 owns bounded task-local batches; gl_rmain.c:1166–1226 begins/flushes/ends them at native material/model boundaries. | Existing dedicated source/software evidence remains limited as recorded in the alias review; final migration checks still required. |
| PERF-018–019 precision | gl_vidsdl.c:1530–1553 keeps native negotiated A2B10G10R10 and D32S8 preference/fallback. | Actual desktop/XR attachment/postprocess coverage; format selection alone is not a rendered result. |
| PERF-021 single-pass stereo | gl_vidsdl.c:2076–2079 requires two-view multiview and sufficient descriptors; Shaders/stereo.inc uses independent gl_ViewIndex eye transforms in the shared shader variants. | Final representative real two-layer rendering; backend submission count alone is insufficient. |
| PERF-022 mj4m1 work reduction | r_alias.c:1146–1174 rejects loaded offscreen models before skin/pose preparation; shared culling/batching/loading above remain active. | Final map correctness/software load and exit; user measures actual frame-time benefit. |
| PERF-023 additional bounded gains | gl_mesh.c:1491–1502 compares exact untracked pose/cache identity before native BLAS work; :1662/1767 reuse matching poses. Tracked palettes deliberately bypass this cache. CPU polygon release and earlier alias rejection also reduce work through existing owners. | Final cache invalidation and unchanged scene behavior; no numeric improvement claimed. |
| PERF-009 native jumbo lightmap packing | r_brush.c:1994..2087 GL_SortSurfaces is text-identical to pinned vkQuake, ordering styles/submodels/Morton locality before its shelf allocator. AllocBlock:1310 retains native shelves/bin placement; its sole native delta factors compute-input allocation into GL_AllocateLightmapInputs. | Final atlas/layout and animated moving-brush correctness. Ironwail size-order packing is not substituted because native grouping feeds one coordinate space per compute workgroup. |
| PERF-014 native brush material grouping | r_brush.c:1053..1166 retains bounded indirect material/atlas/alpha/cutout/liquid/sky groups, descriptor reuse and indexed-indirect emission. foveation filters consume the same groups; direct r_world.c:1234..1493 flushes at material/atlas boundaries. | Final desktop/stereo direct/indirect, liquid/OIT and decal equivalence. No new bindless renderer is required by source evidence. |
| PERF-015 shared GPU lightmap updates | r_brush.c:3912..4128 retains native style/active-light dirty regions and current/previous moving-submodel transforms; :3751/:3833 preserve image barriers, :3901 the indirect/vertex visibility barrier. update_lightmap.inc:289 admits either actual eye while desktop retains center-facing math. | Final style interpolation/dynamic light expiration/moving brush and both-eye images. See existing stereo lightmap review for historical shader ABI evidence; this inspection runs no shader tools. |
| PERF-016 native no-VIS behavior | r_world.c:994..1043 selects native Mod_NoVisPVS/eye union, :477..567 uses native leaf culling and atomic surface bits with dependency marks; :1113..1194 retains worker/serial and indirect/direct paths. Native compute has no donor no-dynamic-light restriction. | Final no-VIS sky/liquid/efrag/light/oldskyleaf and serial/worker/direct/indirect correctness. Primary r_world.c:1062..1092 specialized GL GPU cache is not copied into a second cache/renderer. This does not prove equal CPU cost to that specialized path. |
| PERF-020 Ironwail clip-bias adapter | Shaders/world.vert:45..62 masks the reserved instance bit before indexing and applies reversed-Z -1/1024 after stereo correction, matching Ironwail gl_shaders.h:518..525 math. r_brush.c:1152 and r_world.c batch eligibility retain non-world/non-decal/map_checks rules through current native material paths. | Existing z-fix disposition remains authoritative; final door/lift/decal/near-far/eye imagery remains open, no visible-equivalence claim from math alone. |

These actual jobs are already present; importing another scheduler, loader or
particle renderer would duplicate working native systems. The particle lifetime
patch only repairs allocation and reader ordering at the existing native owner.
Foveation must never narrow visibility to the gaze region: both eye-visible
world regions remain rendered, with shading density independently selected.

The additional packing/grouping/lightmap/no-VIS/clip-bias source comparison above
records actual native data and draw owners, not just related symbol names. Their
final software/visible equivalence and optional graphics coverage remain open. Likewise
the heap/loading/performance outcome rows remain unproven until relevant map
software checks; quantitative speed/RSS and headset measurements are user-deferred
and excluded from completion gates. Historical measurement protocols remain
available as optional references, not required implementation work.

Final software checks are consolidated after all implementation: Linux x86-64,
then isolated native Linux ARM on Foundry. Windows and user live headset/eye/
multiplayer/performance testing remain deferred. No tests/builds/benchmarks were
run for this source checkpoint.

## Native CSQC picture and clip source checkpoint

MOD-003 already has its native/VR drawing adapter; this is source inspection,
not another drawing implementation. Main compared primary pr_cmds.c:3778–3813
with current pr_ext.c:5516–5563: full pictures, image sizes and source-subrectangle
arguments retain their existing cache/mutex/Draw_SubPic owner. In
gl_draw.c:856–940, source size becomes an endpoint once, then maps through the
picture's padded sl/tl/sh/th region; rgb/alpha and native picture/filter pipeline
submission remain. No parallel picture cache or per-eye QC invocation is added.

The existing desktop clip adapter at pr_ext.c:5432–5491 scales the native CSQC
rectangle, intersects before Vulkan integer conversion and resets the full
scissor. VR instead uses GL_SetUIPanelSourceClip/GL_ClearUIPanelSourceClip;
gl_draw.c:1211–1265 intersects finite source rectangles with the current canvas
clip, handles empty regions and clears the source clip. CANVAS_CSQC at1476
retains the current display's pixel scale. Existing gl_screen.c classic/CSQC
panel preparation and GUI error cleanup own the display override/panel lifetime.
This is actual source-path evidence only; final Linux/ARM software checks still
need crop/padding/alpha, resize, clip-reset/error and stereo panel coverage. No
builds/tests/probes/shader tools or images were run/generated for this checkpoint.

## Existing XR mirror, mask and native target source checkpoint

VR-015 and VR-016 have production owners already; their old source-map status
does not imply another renderer implementation is needed. Current source reads
confirmed the following paths without executing them:

| Boundary | Existing implementation and remaining qualification |
| --- | --- |
| Optional mirror | `GL_RecordXRMirrorSnapshot` copies the selected completed eye after `R_RecordFrame`, restores its color-attachment layout, and reserves a native frame slot. `GL_EndXRFrame` calls `VRXR_EndFrame` before `GL_PresentXRMirror`; WSI uses zero-timeout acquisition and polls the pending acquisition fence. Optional mirror errors disable the mirror, preserving XR ownership. See [mirror source review](vr-mirror-senior-review.md) for semaphore, snapshot lifetime and allocation dispositions. Final software lifecycle/image checks remain. |
| Hidden-area coverage | `VRXR_GetHiddenAreaMesh` refreshes bounded per-eye visibility triangles; `GL_PrepareHiddenAreaMesh` packs both asymmetric projections. `GL_OpenXRHiddenAreaWorldEligible` gates the existing stencil writer/masked opaque color and AO depth replay. Native MSAA, OIT and KHR rate variants are implemented; an actual coarse FB/META frame deliberately bypasses world rejection. Final black coverage runs after postprocess/UI independently. See [mask design and review](migration-hidden-area-mask.md); conservative visible-edge/effect qualification remains. |
| MSAA and precision | `GL_SelectRenderFormats` and `GL_SelectNativeSampleCount` retain native format/sample negotiation. `GL_CreateColorBuffer` creates two-layer stereo color targets with native resolved/MSAA attachments; the pass compiler retains resolve attachment topology and optional foveation sample eligibility. Use native `vid_fsaa` and format negotiation rather than restoring OpenGL-only policy or duplicate legacy settings. Final actual attachment, resolve, postprocess, AO/OIT and dark-ramp/alpha-edge software coverage remains. |

These reads confirm implementations and their deliberate boundaries, not image
parity, measured savings, or device readiness. Do not remove the coarse-density
mask gate without a conservative fragment-footprint proof. User headset and
performance measurements remain later follow-up, outside completion; consolidated
Linux/ARM software qualification remains required after all implementation.

## XR-012 source gap: pipeline-cache persistence

Main checked actual native graphics/base-alternative/compute creation in
`gl_rmisc.c:3188,3207,3246`: each passes a null driver-cache handle. Whole-source
and pinned donor searches find no `VkPipelineCache` owner. Existing eager
`R_CreatePipelines` and format/sample/pass setup already provide native warmup;
`R_PrepareStereoView` already uses the shared native dynamic uniform descriptors.
Those owners do not require replacement, but the retained XR driver-cache
behavior is a concrete remaining implementation gap. The
[before-code design brief](native-pipeline-cache-2.0-plan.md) compares translating
the archived XR header/device/CRC/atomic persistence helpers at this boundary
with duplicating a renderer/pipeline registry. No production cache edits or
startup/performance measurements are claimed; source-design review precedes
delegation, and software qualification follows complete implementation.
