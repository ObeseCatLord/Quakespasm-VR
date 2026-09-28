# Renderer, loading and asset feature map

vkQuake already supplies most requested loading and asset mechanisms. Reuse its renderer, task graph, model loader, texture manager, and allocators. Ironwail is a reference for specific differences; its renderer should not become a parallel implementation.

Evidence is from pinned Git objects: **I** = Ironwail `08d578136ff43d7d1ef38e636dfbfd3e844be7cd`; **V** = vkQuake `4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`; **F** = source master `1327f795`; **X** = source OpenXR `30808413`. Paths below are relative to those repositories; unqualified filenames are under `Quake/`. Line numbers refer to pins.

**P** = donor implementation present; **S** = present but requires stereo adaptation; **M** = missing from inspected donors; **U** = outcome/equivalence unverified. Acceptance cases are proposed, **not executed**. No edits, builds, tests, agents, or remote actions occurred.

| ID | Behavior | Source/donor evidence | Disposition | Smallest destination change | Acceptance case |
|---|---|---|---|---|---|
| PERF-001 | Worker scheduling | V `tasks.c:296` `Task_Worker`; worker creation `:453` | P | Retain V scheduler and dependency ownership. | Multiple workers execute indexed jobs; joins complete. |
| PERF-002 | Multithreaded rendering | V `gl_rmain.c:1556` rendering task graph; `gl_screen.c:1546` requires workers, `r_tasks`, GPU lightmaps | S | Extend existing frame/command contexts for eyes; preserve dependencies. | Correct desktop/both-eye images; record worker utilization and frame times. |
| PERF-003 | Parallel texture loading/uploads | V `gl_model.c:1258` submits `Mod_LoadTextureTask`; `gl_rmisc.c:758` staging allocation/copy synchronization | P | Keep texture-manager and staging ownership. | Parallel/serial loads produce identical textures without upload corruption. |
| PERF-004 | Parallel model skins | V `gl_model.c:3832` `Mod_LoadSkinTask`; `:5323` `Mod_LoadMDXSkinTask` | P | Retain indexed skin jobs and joins. | Multi-skin MDL/MD3/MD5 assets match serial loading. |
| PERF-005 | Parallel BSP calculations | V `gl_model.c:1816` `Mod_CalcSurfaceExtentsTask`, submission `:1989`; lump sequence `:3150` remains ordered | P | Reuse jobs; preserve BSP load ordering. | Same extents/lightmaps across worker counts, including mfxsp17. |
| PERF-006 | Avoid fixed-hunk dependence | I `zone.c:562` grows hunk segments; F `zone.c:529` already segmented; V `mem.c:88` dynamically allocates, `gl_model.c:424` frees models | P | Keep V `Mem_*` and model lifetimes; do not import another allocator. | Repeated large-map transitions release model allocations. |
| PERF-007 | Compact marksurfaces | I `gl_model.h:192`, V `gl_model.h:226`: integer indices; F `gl_model.h:216`: pointers | P | Preserve V indexed representation at source integration boundaries. | Identical visible surfaces; measure allocation bytes. |
| PERF-008 | Avoid retained CPU polygon copies | I `gl_model.h:137` surface layout lacks `polys`; `r_brush.c:578` builds vertices from edges. V `gl_model.h:181` retains polys | A, adapter built; runtime proof pending | Current branch releases ordinary polys after Vulkan upload, retaining tiled polys and recovering positions from BSP edges for decals/showtris. | Lower measured resident/peak memory with unchanged rendering, decals, and debug outlines across a map reload. |
| PERF-009 | Jumbo lightmap packing | I `r_brush.c:324` `GL_PackLitSurfaces`: size-based radix ordering; V `:1799` `GL_SortSurfaces`: styles/submodel/spatial ordering | P, different algorithms | Retain V packing, which serves its update layout. | Compare atlas count, packing time, and lighting correctness. |
| PERF-010 | Jumbo BSP formats | I `gl_model.c:2412`, V `:3106` `Mod_LoadBrushModel`: BSP29, 2PSB, BSP2 paths | P | Preserve V widened formats and loader. | Large BSP2 loads with correct surfaces, leaves, and submodels. |
| PERF-011 | Named maps without `-heapsize` | Allocator evidence PERF-006; I README names `tershib/shib1_drake`, `peril/tavistock` | U | First validate existing V allocation behavior. | Both maps reach playable state without `-heapsize`; record peak RSS. |
| PERF-012 | Reduced jumbo loading time | Concrete mechanisms PERF-003–010; no timing evidence inspected | U | Measure existing stages before selecting further changes. | Same assets/settings: cold/warm load durations, stage times, peak memory. |
| PERF-013 | PVS/frustum/backface culling | I `r_world.c:58` GPU marking; V `r_world.c:459` `R_MarkLeafsSIMD`; `Shaders/indirect.comp:68` visibility/backface rejection | S, two-eye indirect plane adapter built | Keep V marking/indirect chain; represent visibility conservatively for both eyes. | No geometry disappears at eye/frustum/PVS boundaries. |
| PERF-014 | Brush batching | I `r_world.c:231` `R_FlushBModelCalls`, bindless multidraw `:263`; V `r_brush.c:1019` indirect draw per material group | S | Reuse V groups/descriptors; preserve material and transparency boundaries. | Equal scene content; capture draw counts and CPU/GPU costs. |
| PERF-015 | GPU lightmap updates | V `r_brush.c:3590` `R_UpdateLightmapsAndIndirect`; I `gl_shaders.h:641` instead combines style samples while shading | A, partial; visual proof pending | Retain V updater and shared lightmap; current branch admits dynamic lights for surfaces facing either eye through its existing compute push constants. Rerelease entity lights use the nearer eye for fade and survive either-eye distance rejection. See [stereo lightmap review](migration-lightmap-stereo-review.md). | Animated/dynamic lighting agrees between eyes, including moving brush models. |
| PERF-016 | Existing no-VIS optimization | F `r_world.c:712` `R_EnsureNoVisSurfaceCache`; `:1062` GPU path has no-VIS and lighting restrictions | P, source | Preserve observable no-VIS behavior through V visibility machinery; avoid duplicating caches. | No-VIS map retains sky, liquids, dynamic lights, and fallback correctness. |
| PERF-017 | Alias instancing | I `r_alias.c:296` `R_FlushAliasInstances`; F `:415` `GL_AliasInstanced_Flush`; V originally drew one instance | A, bounded MDL/MD3 adapter; Linux build and command capture checked | Reuse V preparation, vertex streams, dynamic UBO and per-task contexts. Adjacent opaque surfaces with matching bound poses/materials share an instanced draw; each retains transform, shade, light and blend. MD5 and special/transparent draws stay immediate. See [senior review](migration-alias-instancing-review.md). | Compare repeated models with different transforms, lighting and blends against the pre-change shader; preserve skins/fullbrights, exclusions and both-eye projection. The user measures frame-time benefit later. |
| PERF-018 | Higher color precision | I `gl_rmain.c:226` RGB10_A2; V `gl_vidsdl.c:1518` RGBA8 fallback, A2B10G10R10 selection | S | Preserve V format negotiation through stereo intermediates/presentation. | Dark gradients match intended precision; record actual attachment formats. |
| PERF-019 | Higher depth precision | I `gl_rmain.c:227` D24S8, `:771` conditional reversed-Z; V `gl_vidsdl.c:1539` prefers D32S8; `gl_rmain.c:302` reversed infinite projection | S | Adapt asymmetric eye projections to V depth convention. | Near/far geometry, weapons, decals, and both-eye depth remain correct. |
| PERF-020 | Precise original-level z-fighting workaround | I `gl_shaders.h:518` signed clip-space `1/1024` bias; `r_world.c:480` excludes world/decals. V `r_brush.c:987` used raster depth bias; F `r_brush.c:184` shifts origin | A, adapter added; visual parity pending | The current world shader applies I's reversed-Z clip offset through the existing push layout to eligible non-world brushes and liquids; desktop and stereo share it. See [review disposition](migration-zfix-review.md). | Original-level doors/lifts at grazing angles: stable overlap, unchanged geometry, both eyes. |
| PERF-021 | Single-pass VR | No multiview/view-mask/ViewIndex implementation found in inspected I/V renderer/shader families; V `Shaders/indirect.comp:13` carries one view origin | M | Extend existing V passes/shaders/targets; representative task-enabled opaque multiview is a P1 exit gate before bulk ports. | Eligible opaque geometry uses multiview with distinct eye layers; permitted per-eye transparency/UI remains correct. One queue submission alone is not proof of single-pass rendering. |
| PERF-022 | mj4m1 culling/batching result | One Linux desktop startup-view probe below: about 1,233 alias candidates, 14 accepted model submissions; early alias cull now skips setup for rejected loaded models | U; no measured speedup | Measure the early-cull gain before further alias work; establish a route baseline. | Repeatable route: correct visibility plus CPU/GPU p50/p95/p99, draws, missed VR frames. |
| PERF-023 | Novel gains in desktop and VR | No measured bottleneck or numeric improvement target supplied | U | Profile existing owners; select one bounded change from evidence. | Demonstrated improvement against pinned baseline, with both modes checked for regressions. |
| ASSET-001 | PNG/TGA/JPG/JPEG decoding and lookup precedence | V `image.c:156` originally lacked jpeg and prefers PNG over TGA; F/X `image.c:196` includes jpeg and prefers TGA at equal path priority; higher path priority wins in both | A, adapter present; runtime acceptance pending | Current `Quake/image.c` searches PNG, TGA, JPG, JPEG, PCX and LMP in that order, retaining the highest path ID and decoding JPEG through stb_image. Keep this vkQuake loader and tie precedence. | A .jpeg-only replacement loads; competing PNG/TGA files choose PNG at equal path priority and the higher-priority path otherwise. |
| ASSET-002 | MD3 truecolor skins | V `gl_model.c:5990` shared `Mod_LoadMDXSkinsByIndex`; loader `:6025`; F loader `:4769` | P | Reuse V MD3/material path and retain source-required naming behavior. | Multi-surface MD3 renders correct PNG/TGA/JPG skin selections. |
| ASSET-003 | MD5 truecolor skins | V `gl_model.c:5571` calls shared skin loader; F `:5406` `Mod_MD5LoadSkinFrame` | P | Preserve V parsing/skin ownership; adapt source consumers narrowly. | Animated MD5 retains materials and skin/framegroup selection. |
| ASSET-004 | Model fullbrights | V `gl_model.c:3569` `Mod_LoadFullbrightTexture`; suffix searches `:5257`; `r_alias.c:573` consumes `fbtextures` | P | Reuse glow/luma lookup and mask semantics. | Dark scene preserves emission; transparent mask pixels contribute black. |
| ASSET-005 | Truecolor level textures/fullbrights | V `gl_model.c:1050` map/global texture search, `:1069` glow/luma; F equivalent search `:1925` | P | Preserve V texture manager and map precedence. | Map-local override wins; fences, liquids, and glowing surfaces look correct. |
| ASSET-006 | Truecolor static models | V `gl_model.c:3671` external MDL skins; brush textures use `Mod_LoadTextureTask:979`; static entities enter via `r_world.c:927` | P | Use existing format loaders for static entities. | Same model placed statically/dynamically has matching materials. |
| ASSET-007 | External WAD3/per-texture palettes | V `gl_model.c` `Mod_LoadWadTexture` and `Mod_MissingExternalMiptex`; `gl_texmgr.c` `TexMgr_LoadImage8Valve`; F WAD path | A; runtime image proof pending | Retain WAD/texture owners. The external loader validates the physical lump span and each mip offset, repacks padded levels into vkQuake's compact upload layout, and reads the palette count after the final source mip. Compact textures retain one contiguous read. Missing or rejected external-only entries keep PNG/TGA/JPG override lookup and valid placeholder pixels. Full 16-byte names remain terminated in the internal texture. | Two textures sharing indices but different palettes render distinct correct colors; padded WAD mips and full-width names load; missing/malformed entries never read nonexistent inline BSP pixels, and image overrides still load. |
| ASSET-008 | Lightmapped liquids | I `gl_model.c:1382`; V `:1941` distinguishes lit/unlit liquids; V `r_world.c:1326` binds liquid lightmap; F `gl_model.c:2638` already detects lit water | P | Retain V surface flags and water draw path. | Lit liquid shows baked lighting; classic unlit liquid remains correct. |
| ASSET-009 | Lightstyle interpolation | I `gl_rlight.c:49`; F `:48`; V `R_AnimateLight:41`, former GPU-update gate `:67` | A, adapter added; runtime parity pending | V lighting owner now applies modes 0/1/2 on both CPU and GPU lightmap update paths; retain its existing `r_dynamic` policy. | With r_dynamic=1, smooth and abrupt styles (including ad_tears) follow modes 0/1/2 on both CPU and GPU lightmap update paths; mode 1 retains abrupt flicker. |

The asset evidence establishes implemented formats and lookup behavior, not complete QSS compatibility. Keep original file attribution and established subsystem ownership.

The current BSP texture loader also checks the file's 15 lump spans before
loading, bounds reads from each texture-table entry and payload to the texture lump,
reads Quake 64 mip offsets after its `shift` field, and keeps inline Valve
palette payloads in memory for safe texture reloads. A read-only scan of 1,088
installed BSPs found 49,583 texture entries: eleven already-invalid zero-size
entries, two truncated inline payloads, and no nonzero dimensions failing the
64-pixel mip allocation rule. This is compatibility evidence for the installed
data, not a runtime image comparison or a guarantee for other mods.

## `mj4m1` alias culling probe

A local Linux desktop probe loaded the installed `mjolnir/mj4m1` assets in a
disposable game-data shadow, held the startup view, and sampled frames near
frame 81. `scr_speeds 2` reported about 1,233 alias passes per frame over ten
frames. That counter is incremented for each alias entity *attempted* by
`R_DrawEntitiesOnList`, even if `R_DrawAliasModel` subsequently culls it. A
separate debugger count at `R_DrawAliasSurfaces` in the corresponding startup
view found **14 accepted alias model submissions** across six unique model
names. Six submissions were one rope model; the other five models had one or
two each. `R_DrawAliasSurfaces` can draw multiple surfaces per submission, so
this is **not** an exact Vulkan draw-call count. The sampled view gives little
reason to import generic alias instancing as the first large-map optimization.

The alias path previously selected/checked model skin data and computed pose
and interpolated transform before frustum culling. `R_DrawAliasModel` now uses
the same cull before that work for loaded models, while refreshing any model
marked for reload before testing its bounds. The VR viewmodel still bypasses
this cull, and the existing stereo frustum still covers both eyes. A Linux
build and `mj4m1` load passed; a second startup-view probe reached 15 model
submissions (one extra moving projectile appeared). This is a work-avoidance
change, **not** a demonstrated frame-time gain. The original probe does
**not** establish that culling dominates frame time: the
reported alias-pass count is not a draw-call count, the debugger perturbs
timing, `scr_speeds 2` disables the indirect path, and this is one desktop view
without a repeatable route or headset timing. Keep PERF-017 open for scenes
with genuinely repeated visible models and measure any proposed cull against
frame time and both-eye visibility before claiming a gain.

The `rs_aliaspasses` diagnostic now increments after `R_DrawAliasModel` only
when that call adds model triangles. The startup-view figure above used the
older attempted-entity counter and must not be compared directly with new
`scr_speeds 2` output. The revised value counts accepted alias-model calls,
not exact Vulkan draw commands or GPU fragments; multi-surface models and
separate passes still need their own measurements. This counter correction
does not change culling or rendering.

## Current-branch two-eye indirect culling

The indirect compute pass now tests world-surface backfaces against both actual
eye origins. It rejects a surface only when neither eye can see its front side;
desktop keeps the vkQuake center-origin test. For transformed brush models,
the shader uses the existing uniformly scaled instance transform to map each
world-eye displacement into model space, relative to the already supplied local
center eye. A degenerate transform disables that brush's plane rejection. This
reuses the existing PVS/frustum union, indirect draw pipeline and 64-byte
instance layout; the 56-byte push-constant layout is unchanged. The Linux
shader/executable build and `spirv-val` passed. Moving-brush eye-boundary image
checks and `mj4m1` draw/frame-time comparisons remain open; no speedup is
claimed.

An Astra xhigh read-only senior check of commit `50fbc362` found no concrete
correctness issue in the two-eye brush plane test. It verified that the GLSL
column constructor transposes the stored model-to-world rows, that the CPU
pitch convention and inverse scale agree with the local center, and that the
instance write precedes indirect surface visibility. The strict rejection
keeps a face when either eye is on its plane. The review did not qualify
near-plane floating-point edge images, wider visibility/queue lifetimes, or
GPU cost. Those remain acceptance checks, not assumed wins.

## Current-branch large-map performance hypothesis

`2.0` still inherits vkQuake's ray-shadow path in
`Quake/gl_mesh.c:R_UpdateAnimatedBLASes`: when ray queries are enabled it walks
dynamic and static alias entities, skins their local vertices, and rebuilds or
refits their BLAS during each update. The corresponding TLAS in
`Quake/r_brush.c:R_BuildTopLevelAccelerationStructure` applies entity transforms
separately. This is a candidate for large-map savings, including `mj4m1`,
not a measured speedup.

`885f8aa2` now skips local BLAS skinning/refits when the selected model/geometry,
animation poses and exact finite blend match the last built local vertices.
The first build is always recorded; tracked VRIK palettes and invalid blends
always refit. A moved entity still gets a current TLAS transform, and the
optimization does not rely on caster visibility. The Linux build passed, but
there is no measured speedup or runtime shadow-parity result yet. `scr_speeds 2`
now reports animated BLAS builds, refits and unchanged-pose reuses. Capture
these counts and CPU/GPU frame time on an identical `mj4m1` route
with ray shadows on and off before judging the gain.

Two additional Ironwail candidates qualify as **proposed optional scope**:

| ID | Behavior and implementation evidence | Bounded absence evidence | Disposition / smallest change | Acceptance case |
|---|---|---|---|---|
| CAND-PERF-001 | Surface-anchored dithering: I `gl_shaders.h:727` combines lightmap-coordinate noise with distance-dependent screen noise; `gl_rmain.c:974` supplies amplitudes | V world shading `Shaders/world_common.inc:1` has quantization; `screen_effects.inc:134` has screen noise, not this behavior. Searches A below found no corresponding implementation in V/F/X | Optional, S. Add the formula to the existing world shader; retain current palettization ownership. | Head/camera motion keeps near noise attached to surfaces; inspect stereo coherence and measure cost. |
| CAND-PERF-002 | Clustered fragment lighting: I `gl_rlight.c:134` `R_PushDlights`, compute dispatch `:196`; `gl_shaders.h:1671` cluster construction, `:666` fragment consumption | Searches B below found no cluster-grid implementation in V/F/X. V uses lightmap-space masks (`Shaders/update_lightmap.inc:171`); F/X use `R_BuildLightMap` (`r_brush.c:910`/`:1045`) | Optional, S; defer pending measurements and architecture review. Reuse light ownership; any bounded experiment stays inside existing lighting passes. | Moving lights/brushes match reference appearance in both eyes; measured benefit must justify added complexity. |

Absence searches covered **103 V renderer/shader files and 34 files at each F/X pin**: `Quake/gl_*`, `r_*`, renderer/model/image families, and V `Shaders/`. **A:** `TextureDither|ScreenDither|DITHER_NOISE|lmsize|whitenoise01`; **B:** `LightClusters|lightcluster|cluster_lights|LIGHT_TILES_[XYZ]`; zero matches. These are bounded absence findings, supported by inspecting the alternative lighting/dithering paths.

Generic alias instancing was excluded from optional extras because F and X already implement it. No performance benefit is claimed for either optional candidate.

## Current-branch CPU polygon lifetime adapter

`GL_BuildLightmaps` builds one ordinary `glpoly_t` per face for workgroup bounds
and Vulkan vertex upload. `GL_BuildBModelVertexBuffer` now frees those ordinary
copies after staging the vertex data. Tiled sky, unlit liquids, and missing
textures keep their polys because their runtime paths and a repeated `R_NewMap`
still need them. Decals and `r_showtris` recover ordinary face positions from
the retained BSP surfedge/edge/vertex arrays. `R_NewMap` is the only VBO rebuild
caller and always invokes `GL_BuildLightmaps` first; inline `*` models share
their owning world's surface and edge arrays. The `mj4m1` BSP has about
428,000 faces and an estimated 61 MiB of total `glpoly_t` payload before
allocator overhead, so the release could materially reduce resident memory,
but actual released bytes and RSS are not measured yet.

The Linux build passed. One local `mj4m1` load attempt stopped before map
loading because SDL could not open a video device in the sandbox. The Astra
senior review found no confirmed high-severity regression and recommended the
incremental adapter over a brush-loader rewrite. A running-renderer check of
world and moving-brush decals/showtris before and after a map reload remains
necessary; no frame-time or memory improvement is claimed yet.

| Senior-review concern | Disposition |
|---|---|
| Reupload after releasing polygons | Resolved structurally: the only `GL_BuildBModelVertexBuffer` caller is `R_NewMap`, immediately after `GL_BuildLightmaps`; tiled polys are retained. Runtime reload proof remains open. |
| Linked allocation ownership | Resolved: each `BuildSurfaceDisplayList`/`Mod_PolyForUnlitSurface` node is allocated independently; inline models share the owner's arrays and are skipped during release. |
| Large-face decal coverage | Resolved structurally: ordinary surfaces get one polygon with `numverts == numedges`, so the clipper's existing per-polygon vertex limit is unchanged. |
| Debug geometry equivalence | The BSP endpoint selection matches polygon construction and `showtris.vert` reads position only. Image-level proof remains open. |

## Initial `mj4m1` desktop load observation

On 2026-09-24, the Linux `2.0` Vulkan client loaded the installed 90 MB
`mj4m1.bsp` from a disposable asset-only game-data shadow, then exited with
status 0. A single `xvfb-run` command using `-game mjolnir +map mj4m1 +quit`
took 8.93 seconds wall time and 1,342,160 KB peak child RSS on this machine.
The log reached the map's `Echoes of eternity.` title and allocated 26,755 KB
of lightmap compute data and 93,623 KB of acceleration-structure data. The
timing includes process startup, Vulkan initialization, map loading and
shutdown; it is **not** a map-only loading benchmark, VR frame-time result, or
comparison against vkQuake/OpenGL/Ironwail. Repeatable warm/cold runs and an
in-map route with CPU/GPU frame distributions are still needed before choosing
or claiming a performance optimization.

An isolated build of the exact vkQuake donor commit `4bc898f2` and a frozen
`2.0` binary were then alternated on the same disposable asset shadow. Both
loaded `mj4m1`: donor runs took 6.70 and 6.65 seconds; `2.0` runs took 7.37
and 10.47 seconds. The latter `2.0` run aborted after `Shutting down SDL
sound` with glibc `corrupted size vs. prev_size`; it must not enter a speed
comparison. A debugger run and three address/undefined-sanitizer runs of the
same committed `2.0` code loaded and exited normally, without a sanitizer
finding. The cause remains unresolved, including whether the Vulkan driver,
shutdown ordering, or earlier application memory corruption is responsible.
The old screenshot-task teardown race was already addressed; this evidence
does not identify a recurrence of that same race.

A debugger inspection of the failed run's core placed detection in NVIDIA
process-exit cleanup **after** engine shutdown returned. A diagnostic binary
from the frozen `2.0` commit skipped only final pipeline/stereo-layout
destruction: three of four runs still aborted, versus two of four unchanged
control runs. That rules out those calls as necessary triggers; the diagnostic
binary is not part of branch `2.0`. Four additional `-nosound` runs produced
three aborts, so active SDL sound is not necessary either. These small samples
do not identify the earlier corrupting operation. The three sanitizer runs
cannot exclude mimalloc-managed engine corruption because this build did not
enable mimalloc's ASan allocation tracking. A surviving Vulkan instance,
device, surface and swapchain at process exit make final driver cleanup a
priority boundary for further investigation, but the donor also retains them.
Four further runs of the unmodified vkQuake donor on the same disposable
`mj4m1` fixture exited 0, 134, 134, and 134; all loaded the map, and all
nonzero exits showed the same glibc corruption abort. The failure is therefore
reproducible on the donor baseline too, so these runs do not support calling
it a `2.0` regression. They also do not prove a common root cause. Keep the
affected runs out of load-speed comparisons and investigate teardown at the
shared vkQuake/Vulkan boundary.

A detached diagnostic build at `314b2f06` used `USE_CRT_MALLOC` with
AddressSanitizer and UndefinedBehaviorSanitizer so engine allocations went
through the intercepted C allocator. Three `mj4m1` load-and-quit runs all
loaded the map and exited 0; none reported an AddressSanitizer error. They
did report inherited undefined-behavior diagnostics in image resizing and
model loading (misaligned access and null arguments to nonnull functions).
The sanitizer build changes allocation and timing, and these findings do not
identify the release-only shutdown abort. No diagnostic code was merged into
`2.0`.

The alignment findings prompted a narrow portability fix: packed sprite
headers/intervals now load through aligned copies, and the image-resizer's
scalar coefficient moves use byte-preserving `memcpy`. A full sanitizer build
with only those two production-file changes loaded `mj4m1` and exited 0;
neither prior alignment diagnostic recurred. That run also reported fifteen
UBSan nonnull-argument diagnostics in the MD5 animation parser. A later
source review found no confirmed current null-to-nonnull call: `ddf0079e`
guards the zero-joint bind-pose `memcpy` that previously ran once per pose,
and the current token parser accepts a null cursor. The count is consistent
with the old zero-joint path, but the raw diagnostic locations and a focused
sanitizer reproducer are unavailable, so this is not a verified runtime fix.

`08d38a79` now rejects MD5 animation component counts that exceed the loaded
file or overflow the temporary float allocation, and checks joint component
offsets without overflowing their sum. The Linux build passes. An isolated
address/undefined-sanitizer build also completed, but its graphical run could
not initialize SDL video in this sandbox, so it supplies no runtime sanitizer
result and does not resolve the earlier diagnostics.
This does not establish an ARM runtime result or resolve the shutdown abort.
