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

1. **Existing-record memory upload prerequisite.** Share TexMgr_ReloadImage's
   final native-format upload switch in one private helper. Add one thin
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
