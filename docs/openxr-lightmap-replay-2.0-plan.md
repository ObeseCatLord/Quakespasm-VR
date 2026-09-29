# Lightmap derived-input reconstruction

Status: verified design brief, not implemented. Local Astra disposition precedes
production. This preserves the parent [device reconstruction contract](openxr-device-reconstruction-2.0-plan.md),
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
