# Lightmap derived-input reconstruction

Status: historical stage1/2a/2b prerequisite; unused replay paths will be removed
under the [focused Vulkan scope disposition](openxr-device-reconstruction-2.0-plan.md#focused-astra-max-scope-disposition).
Stage3 is not implemented; general live device replay is deferred. The earlier
design below is superseded by the removal plan at the end. Parent:
[device reconstruction contract](openxr-device-reconstruction-2.0-plan.md),
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

## Stage2a local Astra disposition and revised production contract

Local Astra/max personally verified7e53c098 and accepted the narrow owner design
conditionally. Adopt all findings before production:

| Finding | Disposition |
| --- | --- |
| P1: live R_BuildLightMap consumes entity-space dynamic-light scratch | Add a private option inside the same native calculation: reconstruction builds static/style pixels without dynamic lights or surface-cache updates. Ordinary public R_BuildLightMap retains original behavior. No copied lighting math or temporary live frame/dlight mutations. |
| P1: early cache markers depend on eventual update timing and CPU r_dynamic | Delete all proposed stage2a activation markers. Full base upload, cropped unconditional GPU refresh and CPU cache activation move to the verified stage2b boundary. Preparation alone does not establish a restored frame. |
| P2: freeing temporary arrays is not rollback | Preparation destructively replaces derived CPU inputs. Caller remains quiesced until reconstruction completes or the owner is discarded; no resume-old-renderer cancellation promise or rollback copies. |
| P2: third public cleanup API is unnecessary | Keep transient free helper private and reuse during regeneration/full owner disposal; expose only eligibility and regeneration. Any future external cancellation cleanup needs a concrete owner contract. |
| P2: authoritative sample byte length must share real size arithmetic | Record actual .lit/grayscale/Q64/Valve RGB allocation lengths, use checked size_t products for allocation/copy/offset, guard .lit header reads, clear borrowed inline metadata on full disposal without freeing shared pixels twice. |
| Non-brush precache correction | Skip zero-surface aliases; reject inconsistent non-brush owners with brush surfaces. Match first-null native lightmap traversal and tiled index contributions. Existing geometry validator is an additional prerequisite, not traversal replacement. |

Revised estimate250–350 changed source lines including moved initialization.
Initial-map allocation and dynamic-light behavior remain native. No GPU creation,
record crop/upload replay, cache activation or parent transaction in this slice.
Source-only final review follows implementation; no builds/tests/fixtures yet.

## Stage2b candidate upload/activation brief (awaits separate Astra disposition)

Now that stage2a separates static/style preparation from entity-space dynamics,
this next slice must preserve atlas image identities through the existing native
crop/compaction/upload code. Exact write set r_brush.c/glquake.h and only a narrow
texmgr validation sharing if source evidence requires it; expected180–320 changed
lines including moved setup code. Workgroup GPU allocation/retirement, surface
buffers, descriptors and parent switching remain stage3, not upload readiness
claims. The caller supplies new-device staging/texture owners and workgroup
buffers after old-device retirement, plus prepared stage2a CPU inputs.

Share existing GL_SetupLightmapCompute's actual crop, in-place row compaction,
style-list compaction and workgroup staging in one private setup helper. Initial
setup retains original TexMgr_LoadImage record creation and cache initialization;
replay calls TexMgr_ReplayGeneratedImage on those same records, skips shared
nulltexture planes, restores complete base pixels and frees temporary inputs at
the same copy-complete boundary. No pointer installation or CRC path. The public
initial setup still owns original workgroup buffer allocation. Replay requires
already-created new buffers; owner-local GPU recreation is a distinct contract.

Before mutation/upload, check the complete graph's actual records by stable
owner plus generated native names through TexMgr_FindTexture, compare pointers
before dereferencing supplied record fields, and verify native source format,
source dimensions/crop and permitted immutable borrowed profile. Verify base,
style and index span readiness and new workgroup handles in the replay entry;
require retired fields for every generated record. Eligibility for pre-retirement
checks must not require transient pixels that stage2a has not generated yet or
empty GPU fields on the still-running old device. Native initial setup remains
unchanged for ordinary callers. No claim that old-device capability limits are
proof of a prospective runtime GPU's limits. The parent owns prospective-device
qualification and post-retirement failures; a staging uploader is not rollback.

Activation after full base upload: invalidate per-surface cached_light so the
next correctly contextual CPU render performs native refresh if r_dynamic is on;
when off, the complete uploaded static/style base already supplies the native
output. Do not rely on modified bits with an empty rectchange. Existing surface
cached_dlight becomes false because the restored base contains no dynamic light.
For GPU mode, mark existing atlas cached_framecount explicitly invalid (-1),
then have the existing updater consume that invalid state independently of a
frame increment. It must bypass the modified==0/unchanged-style early exits and
force unconditional regions only within the cropped dispatch domain. Preserve
normal dynamic-light block analysis even on that frame so active_dlights records
where the newly rendered lights must be removed on a later frame. Count blocks
once, preserve native style packing/compute shaders, and clear invalid status
through the existing successful scheduling assignment to cached_framecount.
No separate persistent restoration flag, lightstyle phase reset or GPU timing
assumption. Initial map caches/updates remain their existing policy.

Astra must challenge sentinel/first-update semantics, CPU r_dynamic-off behavior,
null planes, graph-wide rejection before partial upload, compaction exactly once,
record membership/borrowed lifetime and whether any new APIs are necessary. A
preflight rejection preserves all records and inputs; native fatal allocation
errors remain fatal. Final parent qualification at the end is required; this
brief does not authorize production or establish live device reconstruction.

## Stage2a production/source acceptance

Local Astra/max personally accepted the four-file CPU preparation source with
no P1/P2 findings:215 additions/43 deletions, within revised250–350 estimate.
GL_CanRegenerateLightmapInputs and GL_RegenerateLightmapInputs share complete
preflight, native first-null/index/submodel traversal and existing atlas owners.
Shared native lighting calculation omits dynamic-light scratch and cache writes
only during reconstruction; initial and public callers retain their behavior.
Transient allocation/free is shared, retained BSP positions replace only missing
polygon inputs, and authoritative RGB allocation length bounds sample reads.
No extra permanent jumbo-map upload copy, atlas repack or texture record changes.

The original stage2a candidate cache markers/third public cleanup API above were
rejected and superseded by the adopted disposition. Preparation alone is not a
restored frame and cannot resume the old renderer. No external callers, GPU
creation/upload or cache activation were added. Compilation and end-to-end
qualification remain deferred until full implementation, not certified by this
source review. Main reviewed the combined diff and git diff --check passed.

## Stage3 additional GPU source checkpoint

r_brush.c's existing static owners retain surface count, draw membership and
CPU indirect commands, while surface-data/submodel/workgroup/indirect/index/
visibility buffers have separate native memory owners. Frame-upload lightstyle,
light, submodel-transform and bmodel-instance buffers share one allocation and
mapped views. R_AllocateLightmapComputeBuffers remains their startup creator;
full retirement must clear all four buffers/mappings before using it again.
R_FreeBuffers destroys buffers then shared memory but does not clear caller
fields; R_FreeBuffer likewise leaves its by-value buffer field unchanged.

The native GL_BuildLightmaps loop calculates each compute surface's packed
styles, normal/plane, atlas coordinates, texture vectors, indirect membership
and vbo offset. A GPU-only surface restoration should extract those existing
writes into one private shared fill routine, using retained vbo_firstvert during
replay, while initial construction alone still assigns that offset. Restore
surface-submodel tags using the existing progression and preserve the cutout bit.
Reusing UpdateIndirectStructs/GL_SortSurfaces would change membership/packing and
is not indicated. Existing GL_SetupIndirectDraws can then consume retained CPU
commands/dependencies after its own GPU fields have been retired and cleared.
These are source checkpoints; a bounded retirement/recreation contract and local
Astra disposition are still required before any stage3 production edits.

## Stage2b staging visibility verification

Direct source check: TexMgr_LoadImage32's individual image transition names
fragment sampling, but gl_rmisc.c R_SubmitStagingBuffer675..680 already adds a
shared TRANSFER_WRITE to MEMORY_READ|MEMORY_WRITE barrier, TRANSFER to ALL_COMMANDS,
before each staging submission on the existing graphics queue. Reuse that owner;
no second replay barrier or uploader flag is indicated. Parent activation must
actually submit these ordered staging commands before compute/graphics consumers.
The memory dependency requirement follows the official [pipeline barrier
sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html);
[queue idle](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueWaitIdle.html)
establishes host-observed completion and is not the generated upload API's
return guarantee. This source finding resolves the tentative barrier question;
full transaction/validation qualification still follows implementation.

## Stage3 candidate brush GPU owner brief (scope reopened; do not implement)

Verified remaining owner slice: r_brush.c and existing glquake.h declarations;
expected400–600 changed lines including moves. Native buffers and descriptor
layout/allocator/staging helpers remain the owners. No renderer registry,
parallel map data or preserved whole-map upload mirror. GPU retirement runs only
under the parent's joined/quiesced and completed-or-lost submission contract on
the old device, before its allocator/layout/function-pointer owners are retired.
No unconditional nested healthy-device wait in the loss path is proposed.

Local Astra Max returned conditional design acceptance after personally reading
the owners. Main has not begun this stage. The user's subsequent Vulkan-reuse
question reopened whether full device reconstruction belongs in the goal at all;
the [parent scope reassessment](openxr-device-reconstruction-2.0-plan.md#scope-reassessment-brief-preserve-the-donor-renderer)
supersedes this candidate until its scope is resolved. Required corrections if
this optional stage is later pursued: extract GPU-only indirect setup without
PrepareIndirectDraws/R_CalcDeps; prove plane/texinfo/visibility allocation spans;
include bmodel_instances_desc_set and create frame buffers before vertices;
retire independently present partial/duplicate AS children and both TLAS slots
before backing-buffer garbage; resolve TLAS allocation counter mismatch; verify
the first-frame previous-transform shader branches. Do not treat that review as
permission to build a larger renderer or a completion claim.

Retire lightmap and indirect descriptor sets; surface-data/submodel/indirect/
index/visibility buffers plus their native memory; per-atlas workgroup buffers
before shared memory; four mapped frame-upload buffers before their shared
allocation. Clear handles/mappings/visibility offsets at those same owners.
Preserve lightmaps/base pixels/crops/records, num_surfaces, indirect_draws,
initial_indirect_buffer, brush_deps_data and native CPU draw membership. Keep
buffer deletion helpers private unless the parent needs a single owner entry.
Do not mistake R_FreeBuffer's by-value argument for clearing the owner field.
Shared partial allocations must be freed even if a particular buffer is null;
this is an idempotent disposal requirement, not another GPU resource catalog.

Restore surface compute payload through a private extraction of initial
GL_BuildLightmaps's existing field writes. Initial map construction alone
assigns vbo_firstvert and builds draw membership; replay uses retained offsets,
indirect_idx, atlas coordinates and original packed styles/texture vectors.
Restore native surface-submodel tags and cutout bit. Reuse existing allocation,
staging, frame-upload, workgroup creation and GL_SetupIndirectDraws functions
after all old fields are clear. Integrate stage2b image replay before native
descriptor updates and preserve native staging drain before consumer submission.
Preflight includes shader source pointers/plane/texinfo, index/tag/range counts
and retained indirect spans, not just nonnull geometry. This remains one owner
reconstruction entry, without a second command generator or atlas rebuild.

Brush acceleration needs the same old-owner handling: existing creation records
per-model BLAS and buffers for brush precache owners (including inline models),
shared bmodel indices/AS memory and TLAS/buffer/memory. Its current deletion
returns immediately when TLAS is absent and can miss partially initialized
children. Retire each existing nonnull child through that owner, then shared
memory, clearing model addresses/handles. Dynamic TLAS resize already queues old
allocations through existing dynamic garbage; parent must drain every native slot
before device destruction. Alias entity/static/private-prop acceleration remains
its separate existing owner, not an invented qmodel brush allocation. Shared AS
scratch remains R_FreeASScratchBuffer's owner and must not be retired twice.

First compute restoration must account for double-buffered transform history:
current transforms are populated from native live entities; replay's first
unconditional regions must avoid reading an uninitialized previous transform
half for dlight-only differential updates. Stage2b's forced full-refresh frame
is intended to establish that boundary. Existing R_ClearBModelInstanceClaims
latches the compute half and clears claims each frame; do not add a new claim
state machine or reset live entity transforms. Restore old settings/presentation
policy, retaining native ray-query conditional creation.

Astra must challenge ownership/order, partial-init guards, retained CPU readiness,
packed payload equality, GPU-only lifecycle versus native full map cleanup,
first-use history and whether the 400–600-line scope requires splitting. This
brief is verified-source planning only; source acceptance, parent integration
and end-of-implementation Linux/ARM behavior proof remain required.

## Stage2b local Astra design disposition

Local Astra/max personally verified the complete candidate, including staging,
and conditionally accepted the owner-local architecture. Adopt before production:

| Finding | Disposition |
| --- | --- |
| P1: sequential uploader validation can fail after prior uploads | Share the existing TexMgr predicate through one read-only readiness query. Validate all matching records, fresh CPU inputs, retired GPU fields and new workgroup buffer prerequisites before mutation; unexpected later rejection is a fatal invariant failure. |
| P1: submodel lighting can survive skipped atlas frames | On forced refresh, conservatively latch existing active_dlights for in-crop submodel blocks when current lights exist. Retain ordinary intersection/expiration/clearing logic; no new persistent flag or shader. |
| P2: first refresh must survive frame increments | Derive forcefull from exact cached_framecount==-1; bypass both early skips and force region2 only within rounded index crop, counting each block once after native dynamic accounting. Consume through existing scheduling assignment. |
| P2: CPU activation follows complete replay | Invalidate per-surface cached_light and clear cached_dlight after whole staging pass; full base upload supplies static/style lighting even when r_dynamic is off. Empty dirty rectangles are not upload proof. |
| P2: empty planes and compaction lifetime | Empty planes retain shared nonnull nulltexture, skipped for generated lookup/retirement/upload. Validate native width promotion/ceil-to-eight locally; require fresh arrays and zero compact counts. Repeat requires regeneration. |
| Staging memory dependency | Reuse verified R_SubmitStagingBuffer global barrier. Parent drains pending staging before consumer submissions; no duplicate barrier or queue-idle readiness claim. |

One owner eligibility query and one replay operation are justified, plus the
shared TexMgr readiness query; cache activation remains private. Workgroup
buffers must be newly created/bound by the caller after retirement; nonnull
handles do not prove provenance. Pre-retirement query checks source/record
eligibility without requiring transient arrays, retired handles or future-device
readiness. Revised estimate250–350 changed lines including moves. Stage3/parent
still needs its own review; no renderer-ready or rollback claim from upload.

## Stage2b production/source acceptance

Local Astra/max personally accepted the four-file source with no P1/P2 findings:
169 additions/16 deletions. The shared TexMgr readiness predicate validates the
whole record graph before packing/upload; pre-retirement source eligibility is
separate from fresh-input/retired-GPU readiness. Native initial allocation and
record creation remain unchanged. Replay uses existing records, shared crop/
packing/staging/free code and shared null planes. Activation follows the complete
pass; exact cached_framecount==-1 forces one cropped unconditional GPU refresh,
retaining dynamic-light accounting and submodel removal obligations. CPU cache
activation relies on full base upload when r_dynamic is off.

No duplicate barrier, renderer registry, persistent flag or shader change.
Return establishes CPU staging/recording, not GPU completion or renderer readiness.
No production parent callers were added; new bound workgroups, descriptors,
shared placeholder and ordered staging drain remain caller obligations. Main
reviewed the diff and git diff --check passed. No builds/tests/fixtures/compiler
commands ran; end-of-full-implementation qualification remains outstanding.


## Unused lightmap replay removal plan (before implementation)

The focused Astra scope review recommends deleting unused reconstruction-only
paths separately from the alias memory correction. Direct call-site inspection
confirms no production initiator for GL_RegenerateLightmapInputs or
GL_ReplayLightmapInputs; generated-image replay is used only by that unused
lightmap path. The cache-invalid sentinel is set only by its activation helper.

Preserve vkQuake's initial atlas construction, surface data, lightstyle packing,
workgroup bounds, native texture records, GPU updater and desktop/VR lighting.
Retain the shared initial transient allocation/free helpers, checked lighting
loader sizes/header reads and private brush vertex validation/calculation used
by initial uploads and the reduced-CPU-polygon design. No whole-map mirrors or
new lifecycle layer. The model lightdata length/allocation metadata remains at
its current owner; it has no extra payload allocation.

Remove the CPU regeneration eligibility/preparation entries, generated-record
eligibility/upload entries, image replay validation/activation and public
headers. Delete the now-unused public brush vertex eligibility wrapper, retaining
its private validator and the actual native upload. Collapse the lightmap
upload replay flag and static-only CPU-lightmap flag back to the native initial
paths: ordinary R_BuildLightMap updates native caches/dlights as before. Remove
replay-only forcefull branches/sentinel checks from the GPU updater, retaining
all earlier stereo, movable-submodel, lightstyle and dlight scheduling logic.
Keep original upload crop/row compaction/packing and staging barriers unchanged.

Write scope: Quake/r_brush.c, gl_texmgr.c/h, glquake.h and these docs. Main owns
integration. Expected350–500 changed lines, mostly deletion. No models/avatars,
shader algorithms, texture loaders, allocator or map-reset replacement. Astra's
scope disposition authorizes the smaller owner design; final bounded source
review will compare native initial upload and updater branches. Qualification
remains after the full implementation pass; no builds/tests/fixtures now. Full
loaded-scene lighting and desktop/stereo acceptance remain required, while a
new-device replay transaction is a deferred candidate.
