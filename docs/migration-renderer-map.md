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
| PERF-008 | Avoid retained CPU polygon copies | I `gl_model.h:137` surface layout lacks `polys`; `r_brush.c:578` builds vertices from edges. V `gl_model.h:181` retains polys | P, I only | Audit V consumers before removing any demonstrated redundant copy; retain layout initially. | Lower measured resident/peak memory with unchanged rendering. |
| PERF-009 | Jumbo lightmap packing | I `r_brush.c:324` `GL_PackLitSurfaces`: size-based radix ordering; V `:1799` `GL_SortSurfaces`: styles/submodel/spatial ordering | P, different algorithms | Retain V packing, which serves its update layout. | Compare atlas count, packing time, and lighting correctness. |
| PERF-010 | Jumbo BSP formats | I `gl_model.c:2412`, V `:3106` `Mod_LoadBrushModel`: BSP29, 2PSB, BSP2 paths | P | Preserve V widened formats and loader. | Large BSP2 loads with correct surfaces, leaves, and submodels. |
| PERF-011 | Named maps without `-heapsize` | Allocator evidence PERF-006; I README names `tershib/shib1_drake`, `peril/tavistock` | U | First validate existing V allocation behavior. | Both maps reach playable state without `-heapsize`; record peak RSS. |
| PERF-012 | Reduced jumbo loading time | Concrete mechanisms PERF-003–010; no timing evidence inspected | U | Measure existing stages before selecting further changes. | Same assets/settings: cold/warm load durations, stage times, peak memory. |
| PERF-013 | PVS/frustum/backface culling | I `r_world.c:58` GPU marking; V `r_world.c:459` `R_MarkLeafsSIMD`; `Shaders/indirect.comp:68` visibility/backface rejection | S | Keep V marking/indirect chain; represent visibility conservatively for both eyes. | No geometry disappears at eye/frustum/PVS boundaries. |
| PERF-014 | Brush batching | I `r_world.c:231` `R_FlushBModelCalls`, bindless multidraw `:263`; V `r_brush.c:1019` indirect draw per material group | S | Reuse V groups/descriptors; preserve material and transparency boundaries. | Equal scene content; capture draw counts and CPU/GPU costs. |
| PERF-015 | GPU lightmap updates | V `r_brush.c:3590` `R_UpdateLightmapsAndIndirect`; I `gl_shaders.h:641` instead combines style samples while shading | S | Retain V updater; share lighting work using visibility from both eyes. | Animated/dynamic lighting agrees between eyes, including moving brush models. |
| PERF-016 | Existing no-VIS optimization | F `r_world.c:712` `R_EnsureNoVisSurfaceCache`; `:1062` GPU path has no-VIS and lighting restrictions | P, source | Preserve observable no-VIS behavior through V visibility machinery; avoid duplicating caches. | No-VIS map retains sky, liquids, dynamic lights, and fallback correctness. |
| PERF-017 | Alias instancing | I `r_alias.c:296` `R_FlushAliasInstances`; F `:415` `GL_AliasInstanced_Flush`; V `:174` draws one instance | S | Adapt existing batching eligibility within V alias drawing. | Repeated models retain skins, poses, colors, fullbrights; capture submissions. |
| PERF-018 | Higher color precision | I `gl_rmain.c:226` RGB10_A2; V `gl_vidsdl.c:1518` RGBA8 fallback, A2B10G10R10 selection | S | Preserve V format negotiation through stereo intermediates/presentation. | Dark gradients match intended precision; record actual attachment formats. |
| PERF-019 | Higher depth precision | I `gl_rmain.c:227` D24S8, `:771` conditional reversed-Z; V `gl_vidsdl.c:1539` prefers D32S8; `gl_rmain.c:302` reversed infinite projection | S | Adapt asymmetric eye projections to V depth convention. | Near/far geometry, weapons, decals, and both-eye depth remain correct. |
| PERF-020 | Precise original-level z-fighting workaround | I `gl_shaders.h:518` signed clip-space `1/1024` bias; `r_world.c:480` excludes world/decals. V `r_brush.c:987` uses depth bias; F `r_brush.c:184` shifts origin | S, non-equivalent | Translate I bias and eligibility into existing V shaders if exact behavior is required. | Original-level doors/lifts at grazing angles: stable overlap, unchanged geometry, both eyes. |
| PERF-021 | Single-pass VR | No multiview/view-mask/ViewIndex implementation found in inspected I/V renderer/shader families; V `Shaders/indirect.comp:13` carries one view origin | M | Extend existing V passes/shaders/targets; representative task-enabled opaque multiview is a P1 exit gate before bulk ports. | Eligible opaque geometry uses multiview with distinct eye layers; permitted per-eye transparency/UI remains correct. One queue submission alone is not proof of single-pass rendering. |
| PERF-022 | mj4m1 culling/batching result | Mechanisms PERF-013–017 exist; no mj4m1 measurements inspected | U | Establish baseline before modifying those mechanisms. | Repeatable route: correct visibility plus CPU/GPU p50/p95/p99, draws, missed VR frames. |
| PERF-023 | Novel gains in desktop and VR | No measured bottleneck or numeric improvement target supplied | U | Profile existing owners; select one bounded change from evidence. | Demonstrated improvement against pinned baseline, with both modes checked for regressions. |
| ASSET-001 | PNG/TGA/JPG/JPEG decoding and lookup precedence | V `image.c:156` originally lacked jpeg and prefers PNG over TGA; F/X `image.c:196` includes jpeg and prefers TGA at equal path priority; higher path priority wins in both | A, adapter present; runtime acceptance pending | Current `Quake/image.c` searches PNG, TGA, JPG, JPEG, PCX and LMP in that order, retaining the highest path ID and decoding JPEG through stb_image. Keep this vkQuake loader and tie precedence. | A .jpeg-only replacement loads; competing PNG/TGA files choose PNG at equal path priority and the higher-priority path otherwise. |
| ASSET-002 | MD3 truecolor skins | V `gl_model.c:5990` shared `Mod_LoadMDXSkinsByIndex`; loader `:6025`; F loader `:4769` | P | Reuse V MD3/material path and retain source-required naming behavior. | Multi-surface MD3 renders correct PNG/TGA/JPG skin selections. |
| ASSET-003 | MD5 truecolor skins | V `gl_model.c:5571` calls shared skin loader; F `:5406` `Mod_MD5LoadSkinFrame` | P | Preserve V parsing/skin ownership; adapt source consumers narrowly. | Animated MD5 retains materials and skin/framegroup selection. |
| ASSET-004 | Model fullbrights | V `gl_model.c:3569` `Mod_LoadFullbrightTexture`; suffix searches `:5257`; `r_alias.c:573` consumes `fbtextures` | P | Reuse glow/luma lookup and mask semantics. | Dark scene preserves emission; transparent mask pixels contribute black. |
| ASSET-005 | Truecolor level textures/fullbrights | V `gl_model.c:1050` map/global texture search, `:1069` glow/luma; F equivalent search `:1925` | P | Preserve V texture manager and map precedence. | Map-local override wins; fences, liquids, and glowing surfaces look correct. |
| ASSET-006 | Truecolor static models | V `gl_model.c:3671` external MDL skins; brush textures use `Mod_LoadTextureTask:979`; static entities enter via `r_world.c:927` | P | Use existing format loaders for static entities. | Same model placed statically/dynamically has matching materials. |
| ASSET-007 | External WAD3/per-texture palettes | V `gl_model.c:817` `Mod_LoadWadTexture`, palette flag `:852`; `gl_texmgr.c:1452` `TexMgr_LoadImage8Valve`; F WAD path `:1620` | P | Retain WAD/texture owners; preserve source validation requirements. | Two textures sharing indices but different palettes render distinct correct colors. |
| ASSET-008 | Lightmapped liquids | I `gl_model.c:1382`; V `:1941` distinguishes lit/unlit liquids; V `r_world.c:1326` binds liquid lightmap; F `gl_model.c:2638` already detects lit water | P | Retain V surface flags and water draw path. | Lit liquid shows baked lighting; classic unlit liquid remains correct. |
| ASSET-009 | Lightstyle interpolation | I `gl_rlight.c:49`; F `:48`; V `R_AnimateLight:41`, former GPU-update gate `:67` | A, adapter added; runtime parity pending | V lighting owner now applies modes 0/1/2 on both CPU and GPU lightmap update paths; retain its existing `r_dynamic` policy. | With r_dynamic=1, smooth and abrupt styles (including ad_tears) follow modes 0/1/2 on both CPU and GPU lightmap update paths; mode 1 retains abrupt flicker. |

The asset evidence establishes implemented formats and lookup behavior, not complete QSS compatibility. Keep original file attribution and established subsystem ownership.

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
