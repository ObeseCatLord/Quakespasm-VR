# Lightmap derived-input reconstruction

Status: stage1 source integrated with final local Astra acceptance; stage2a
verified brief awaits disposition. Full lightmap/device replay remains open. This preserves the parent [device reconstruction contract](openxr-device-reconstruction-2.0-plan.md),
with the [brush vertex prerequisite](openxr-brush-vertex-replay-2.0-plan.md)
already source integrated. Only2.0 edits; primary, donor and game assets remain
read-only. No builds/tests until the entire implementation is finished.

## Behavioral reference and demonstrated boundary

Preserve vkQuake's lightmaps, color precision, lightmapped liquids, animated
lightstyles, dynamic lights and movable-brush coordinate spaces in desktop/VR.
Device reconstruction must retain CPU model/surface/texture-record identity,
existing atlas assignments and native rendering policy. It must not call
R_NewMap, reset particles/lightstyle phase, or retain another permanent jumbo-map
upload mirror. Reuse the actual native calculations and GPU upload owners.

Direct current2.0 source inspection:

- `r_brush.c:1591` GL_CreateSurfaceLightmap combines R_BuildLightMap,
  R_AssignSurfaceIndex and R_FillLightstyleTextures into original atlas spans.
  Those source surfaces, samples, texture vectors and atlas coordinates remain.
- `GL_SetupLightmapCompute:2470` compacts lightstyle/surface-index rows, frees
  those arrays and workgroup bounds after upload, and overwrites lightstyle
  membership bitmaps with compact lists. It is not an idempotent replay call.
  Lightmap data, rectused extents, texture pointers and global bounds remain in
  the existing lightmap_s owner (`glquake.h:785`).
- R_AssignWorkgroupBounds uses original polygons, which the vertex upload frees.
  Its coordinate-space and culling math remains reusable with checked retained
  BSP positions for missing ordinary polygons.
- GL_BuildLightmaps repacks atlases, resets frame/upload/draw data and allocates
  buffers while traversing surfaces; R_NewMap also resets running scene state.
  Neither is a GPU-only restoration API.
- `TexMgr_LoadImage:1473` creates a new texture record without OVERWRITE; with
  OVERWRITE it can return an equal-CRC record before GPU creation. Neither
  path establishes restoration of retired images.
- `TexMgr_ReloadImage:1559` already restores an existing texture record through
  native format upload functions, including high-precision lightmaps. It obtains
  data from files or source_offset before colormap translation and its final
  upload switch. Source offsets for freed surface-index data are dangling;
  lightstyle metadata points at lm->data, which is not its original payload.

Donor vkquake4bc898f2 has the same existing texmgr reload/upload owner and
surface/style packing calculations. The primary master51b452c0's context
restart keeps texture records and reloads images through their owners; it is a
behavioral reference, not permission to copy its OpenGL context implementation.

## Minimal adapters and staged implementation

1. **Existing-record memory upload prerequisite.** Call the existing native
   upload functions directly, leaving ordinary reload unchanged. Add one thin
   generated-image API accepting a live texture record and a borrowed CPU pixel
   span. Limit it to SRC_LIGHTMAP, SRC_RGBA and SRC_SURF_INDICES; reject invalid
   record/data/dimensions/unsupported format before mutation. Preserve source
   metadata, owner/name, flags, shirt/pants, CRC and record identity. Restore
   logical dimensions exactly as current reload does and invoke those unchanged
   native upload functions directly, bypassing file lookup and CRC cache. The
   caller guarantees a complete readable payload matching recorded dimensions
   and format, and initialized new-device texture/descriptor owners. No source
   pointer installation, new record, registry, serializer or callback protocol.
   Return input rejection versus successful return from the existing upload
   path; existing fatal Vulkan allocation errors are not newly claimed recoverable.
   Write set gl_texmgr.c/.h; expected50–110 changed lines. Existing ordinary
   reload keeps its file/translation/lifetime behavior. Senior review must
   challenge helper necessity, validation bounds and post-upload evidence.

2. **Regenerate transient lightmap inputs in existing owners.** Preserve atlas
   records and surface assignments. Reuse original allocation/initialization,
   surface-index filling and style packing at their existing boundaries.
   Clear compacted membership/counts before rebuilding them from actual styles;
   initialize new workgroup bounds and regenerate with the same coordinate-space
   and culling math, using retained polygons or checked BSP positions. Preserve
   global live lightstyles/frame/particles; invalidate derived lighting caches
   as required for a fully updated first restored frame. Reuse crop dimensions,
   row compaction and existing record format uploads through stage1. Free these
   transient inputs afterward; no lasting style/index/bounds upload mirror.
   Validate atlas rectangles, sample/style spans, existing texture records and
   source/byte dimensions before destructive retirement. Retained base atlas
   data and transient regeneration have separate lifetimes. Write set r_brush.c
   and existing headers, with bounded source metadata changes only if demonstrated
   necessary. Expected180–350 changed lines; a verified narrower brief and
   disposition must precede this stage's production code.

3. **Derived GPU owners under the parent transaction.** Retire surface/indirect/
   workgroup/dynamic-visibility buffers and descriptor sets on the old device,
   clear handles/mappings, preserve CPU draw membership/dependencies, then restore
   surface data and buffer contents through their existing functions. Keep
   shader packing, draw offsets, submodel tagging, CPU tables and SIMD/PVS policy.
   Parent generic texture restoration must skip lightmap-generated records and
   dispatch their existing graph owner once; freed source pointers are not input
   proof. No separate resource registry or atlas repack. This still requires its
   own owner-lifetime brief before production; actual device loss and failure
   unwind remain separate parent stages.

Replacing the lightmap system duplicates working draw/culling/lightstyle policy.
Repacking the map changes derived assignments and resets adjacent live owners.
Keeping upload copies sacrifices heap reductions. The above owner-local adapters
isolate the demonstrated input lifetime and record-creation incompatibilities.
Pause/reopen if production introduces another map/texture state machine, atlas
registry, duplicate packing math or repeated coordination fixes between new layers.

## End-of-full-implementation proof

Compare recreated images and surface/submodel/workgroup inputs with the native
initial paths for static/animated styles, lightmapped liquids, Q64 and 8/10-bit
output, movable/external brush models and sparse style planes. Preserve model,
surface, texture-record and atlas identities, live lighting phase and particles.
Exercise repeated reconstruction, full first-frame refresh, both lightmap update
modes, failed preflight and partial creation cleanup. Qualification must include
the complete parent loaded-scene transaction, not just CRCs or a mock uploader.
Linux/ARM checks follow implementation; user live headset/gaze/performance tests
and Windows builds remain deferred under the established scope.

## Stage2 source-input checkpoint for the narrower brief

Further direct source inspection locates a necessary preflight input boundary:
Mod_LoadLighting atgl_model.c1959 allocates retained RGB samples for .lit,
Quake64, Valve RGB and converted grayscale data, but qmodel_t stores only
lightdata, not its readable byte length. Faces retain interior samples pointers
(`:2509..2515`); mem.h exposes allocation/free, not an authoritative readable
source-size API. A promise to validate sample spans cannot rely on pointer
existence or texture extents alone. Proposed narrow metadata: retain allocated
lightdata byte length alongside that existing model field, set at actual loader
allocation, clear at existing full model disposal, and preserve it in existing
inline-model copies. No second source buffer or allocation registry. This is
stage2 design work; it needs local Astra disposition before production.

The image regeneration path should use existing atlas rectangles, samples and
style arrays, without GL_SortSurfaces or UpdateIndirectStructs. R_FillLightstyleTextures
at1369 both writes packed pixels and marks membership; its MAXLIGHTMAPS/255
termination and original packing must remain one implementation. Reset compact
membership/counts before calling it, then compact once. R_AssignWorkgroupBounds
at1423 must use its original polygon positions or checked BSP positions; keep
existing model-space/mixed-space tagging. Retained global bounds may be reset
and deterministically regenerated under this owner, not mistaken for live
entity state. Cache invalidation must account for the modified==0 early exit
in GPU updating at3983 and CPU per-surface lightstyle caches; resetting cached
values alone does not establish a complete first-frame refresh.

## Stage1 local Astra design disposition

Reviewer: local gpt-6-astra / max, personally verified against a1c1be9f,
primary51b452c0 and donor4bc898f2; no nested delegation or execution checks.
Conditional acceptance with these adopted constraints precedes production:

| Finding | Disposition |
| --- | --- |
| Native upload can mutate borrowed pixels or skip warp payloads | Restrict to NOPICMIP, no PREMULTIPLY/MIPMAP/WARPIMAGE, dimensions within device limits; preserve flags around native alpha detection. No persistent or transient extra copy. |
| Native deletion uses the current device and skips null image views | Require all existing GPU fields cleared by the parent before entry; reject inconsistent or live handles without mutation. |
| Signed staging arithmetic and truncated input | Add size_t data_bytes; overflow-safe source dimensions times four bounded by INT_MAX. Caller verifies atlas record identity, exact shape/format and readability. Skip shared nulltexture planes. |
| Record and pixel lifetime | Check active-list membership under the existing mutex before dereferencing; owner must keep record and pixels stable throughout quiesced reconstruction. Membership is not proof of ownership or protection against recycled records. |
| Success is not GPU completion | False means preflight rejection. Assert native image/view/allocation/sampled descriptor and lightmap target view after normal return; framebuffer/storage descriptor remain null. Native fatal failures stay fatal. CPU input can be released after staging copy returns. |
| Optional shared upload switch adds unnecessary scope | Delete that extraction; reuse TexMgr_LoadLightmap/LoadImage32 directly, preserve ordinary reload verbatim. |

Revised expected source size: 80–110 lines including declaration/comments.
Stages2/3 still require their own verified briefs and dispositions. End-of-goal
validation remains deferred, including the complete parent transaction.

## Stage2a verified CPU regeneration brief

Scope is now deliberately bounded to CPU input preparation; stage2b will join
these inputs to existing crop/upload functions. No new GPU buffers/descriptors,
retirement, device-switch activation or second lightmap registry in stage2a.
Write set: r_brush.c, declarations in glquake.h, lightdata byte-length metadata
in gl_model.c/.h. Expected180–320 changed source lines including moved allocation
code. The earlier combined180–350 estimate is tight; separating CPU preparation
from GPU ownership avoids implementing the latter before its verified contract.

Proposed narrow APIs: GL_CanRegenerateLightmapInputs (read-only pre-retirement
query), GL_RegenerateLightmapInputs (same validation then regenerate), plus
GL_FreeLightmapInputs to release only the existing transient style/index/bounds
arrays on preparation cancellation. They are prerequisites, not a transaction.
Caller quiesces model/atlas owners and frees temporary inputs after upload or
cancellation. Fatal native allocation failures remain fatal.

Share the original AllocBlock's style/index/workgroup allocation and bound
initialization in a private owner helper. Initial map packing still allocates
base lm->data and initializes shelves exactly as before. Regeneration frees
any outstanding transient inputs, allocates fresh ones in existing lm owners,
clears compacted membership/counts, submodel block flags and derived bounds.
Do not change rectused, source assignments or lightmap records. Reuse
GL_CreateSurfaceLightmap and R_BuildLightMap to refresh retained base atlas
pixels at current lightstyle phase, R_AssignSurfaceIndex for original indices,
and R_FillLightstyleTextures for native packing. Membership remains an unpacked
bitmap until the single existing setup/upload compaction step in stage2b.

Model traversal must match GL_BuildLightmaps: precache1..first null; skip inline
shared arrays, process non-inline brush owners; preserve the cumulative surface
index including tiled surfaces, external brush high bit and world submodel tags
using the same firstface progression. Reuse R_AssignWorkgroupBounds's existing
math with an added qmodel input: retained polygon XYZ when present, otherwise
Mod_SurfaceVertexPosition; don't regenerate whole vertex/UV spans or allocate
replacement polygons. Initial path retains its polygon order/number exactly.
External model surfaces remain excluded from dlight bounds as native code does.

Preflight reuses GL_CanRebuildBModelVertexBuffer for retained geometry and world
submodel validity, then checks lightmap_count/array, persistent base pixels,
nonnegative extents, bounded atlas rectangle and blocklights capacity, active
styles below MAX_LIGHTSTYLES and readable styled sample span. Record shape/crop
and retired GPU validation belong to stage2b preflight; source readiness must
not claim complete image/GPU eligibility. Count traversal exactly against
num_surfaces; allow/skip ordinary non-brush precache owners with zero brush
surfaces and reject only inconsistent non-brush owners with brush surfaces. Validate required style/surface extents fit their
existing rectused crop before mutation. Null samples are permitted with native
fullbright/no-samples behavior. A retained sample pointer is checked by integer
address offset and required RGB bytes against the actual owner allocation span;
no subtraction of unrelated C pointers.

Store size_t lightdata_bytes beside qmodel's existing lightdata pointer. Set
from actual allocations in .lit, Q64, Valve RGB and grayscale loader paths;
clear at loader entry and full model disposal. Existing inline struct copies
preserve shared metadata. Use checked/nonnegative native input lengths and
size_t products for that allocation byte length; do not add allocation tracking
or retain extra data. Required per-surface bytes derive from bounded smax*tmax,
three RGB bytes and actual styles terminated at255. No generic memory-size guess.

Invalidate only derived lighting caches after native base refresh: surface
cached_light=-1 and cached_dlight=true for CPU refresh; atlas cached_light=-1,
modified[0]=UINT_MAX, active_dlights bytes=true, cached_framecount=r_framecount
for a complete GPU refresh. This specifically defeats modified==0 and gives
unconditional regions when stale dlights must be removed; stage2b upload must
not clear these restoration markers afterward. Do not reset r_framecount,
d_lightstylevalue, entity/dlight state, particles or map ownership. No new
persistent restoration flag is planned. Astra should challenge these refresh
semantics in both GPU/CPU modes, transient cleanup, traversal and byte-span proof.

End-of-full-implementation checks remain the parent scene transaction plus
native image/input comparison, repeated preparation/cancellation, live phase,
external/movable brush spaces, lightmapped liquids, sparse planes and malformed
span rejection before mutation. No builds or tests during this implementation.

## Stage1 production/source acceptance

TexMgr_ReplayGeneratedImage is source integrated:73 added lines in existing
texmgr.c/.h, direct native upload, ordinary reload verbatim. Local Astra personally
accepted the final source with no P1/P2 findings after verifying membership,
retired handles, borrowed-input restrictions, signed staging bounds, metadata,
native precision and postconditions. No production caller/parent transaction is
claimed. Source inspection and git diff --check only; no builds/tests/fixtures.

## Stage2b/3 GPU owner source checkpoint (not implementation authorization)

Existing r_brush.c owners remain the reconstruction boundary: surface-data and
surface-submodels allocation helpers free their existing buffers; workgroup
allocation frees shared memory but assumes all old per-atlas buffers are already
retired. The current setup call creates new image records, clears modified bits,
compacts membership in place and frees transient inputs after staging. Reusing it
unchanged would lose record identity and restoration cache markers. The next
verified GPU brief must share its actual crop/row-compaction/bounds staging once
between native initial creation and existing-record replay, preserving old-device
retire-before-create ordering directly read from primary gl_vidsdl.c920..936.

R_UpdateLightmapDescriptors references surface-data, surface-submodels, workgroup,
frame-upload, vertex and transform buffers. Record image restoration alone is
not draw readiness. The parent must restore these existing owners before
updating descriptors, and clear old handles/mapped pointers before native
allocation helpers run on the replacement device. GL_SetupIndirectDraws reuses
retained initial_indirect_buffer and indirect_draws, but its index/visibility
allocation helpers also free their old buffer inputs. No new CPU upload mirror
or independent draw-membership rebuild is indicated by this evidence.

Supporting parent-retirement evidence: R_DestroyStagingBuffers currently frees
shared staging memory before destroying its buffers; it does not destroy staging
fences/command pool or clear buffer fields. R_InitStagingBuffers recreates several
mutexes and a condition unconditionally. R_FreeBuffer receives a buffer by value
and leaves the owner field unchanged; R_FreeVulkanMemory does clear its own
handle/size/type. GL_HeapDestroy frees segment-owned arrays and shared segments
array but does not free the heap object or each segment allocation itself.
These are concrete lifetime gaps for later bounded owner retirement, not a
reason to replace staging/heap allocation or add a generic resource registry.
