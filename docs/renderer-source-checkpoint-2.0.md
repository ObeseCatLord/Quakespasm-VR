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

These actual jobs are already present; importing another scheduler, loader or
particle renderer would duplicate working native systems. The particle lifetime
patch only repairs allocation and reader ordering at the existing native owner.
Foveation must never narrow visibility to the gaze region: both eye-visible
world regions remain rendered, with shading density independently selected.

PERF-009 native lightmap packing, PERF-014 brush grouping, PERF-015 lightmap
updates, PERF-016 no-VIS equivalence, PERF-020 clip bias and optional graphics
still require their complete existing-source/software dispositions. This bounded
inspection does not certify them just because related symbols exist. Likewise
the heap/loading/performance outcome rows remain unproven until relevant map
software checks; quantitative speed/RSS and headset measurements are user-deferred
and excluded from completion gates. Historical measurement protocols remain
available as optional references, not required implementation work.

Final software checks are consolidated after all implementation: Linux x86-64,
then isolated native Linux ARM on Foundry. Windows and user live headset/eye/
multiplayer/performance testing remain deferred. No tests/builds/benchmarks were
run for this source checkpoint.
