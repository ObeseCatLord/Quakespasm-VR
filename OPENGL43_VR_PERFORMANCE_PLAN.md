# Quakespasm-OpenVR OpenGL 4.3 and VR Performance Plan

Status: proposed engineering plan  
Date: 2026-08-13  
Targets: Windows x64 and Linux x86-64  
Primary workloads: VR, Mjolnir `mj4m1`, and rerelease MD3/MD5 enhanced models

## Executive decision

Improve the existing OpenGL renderer in measured, independently shippable
phases. Do not begin with a Vulkan rewrite. The highest-value work is to:

1. measure CPU, GPU, and per-eye cost reliably;
2. replace the immediate-mode enhanced-model renderer;
3. share stereo scene preparation and add real model instancing;
4. introduce an OpenGL 4.3 world renderer based on Ironwail's architecture;
5. reduce stereo GPU duplication and make the validated 4.3 path the default.

Native OpenXR is valuable, but it is a separate runtime/backend project. It can
use the OpenGL renderer and should not block or be combined with the five
performance phases.

This project does not target macOS. OpenGL 4.3 support on Windows and Linux may
therefore become a final runtime requirement after the fallback and release
policy in Phase 5 is explicitly approved.

## Findings preserved from the renderer analysis

### Current renderer

- The video layer does not request a particular OpenGL version or profile. It
  obtains a default SDL OpenGL context and conditionally loads VBO functions
  from OpenGL 1.5 and GLSL functions when OpenGL 2.0 is available.
- The renderer is a hybrid of static VBO/GLSL fast paths and compatibility
  OpenGL. Fixed-function matrices, texture environments, alpha test,
  `glBegin`/`glEnd`, and other compatibility features remain in active paths.
- Normal Quake MDL alias models already have a good static-VBO path: pose
  interpolation and lighting happen in GLSL and each entity uses an indexed
  draw. Current `r_alias_batching` sorts compatible entities and retains some
  state, but it still submits one draw per entity.
- MD3 and rerelease MD5 replacements do not use that optimized path. Both walk
  indices on the CPU, interpolate vertices/normals on the CPU, and emit
  immediate-mode triangles. Extra overbright/fullbright passes can repeat the
  work. VR repeats it for both eyes.
- World geometry uses static vertex buffers, but visible surfaces are still
  discovered by classic CPU BSP/PVS traversal and accumulated into mutable
  texture/lightmap chains. CPU-side index batches and frequent material or
  lightmap transitions produce substantial CPU and driver overhead on large
  maps.
- Existing `r_perfdebug` instrumentation records useful CPU sections and
  surface/entity counters, but it does not yet provide a reliable CPU/GPU
  split or a complete per-eye comparison.

### VR renderer

- `VR_UpdateScreenContent` loops over two eyes and enters the ordinary
  screen/view/render path for each eye.
- `R_SetupFrameState` is already restricted to the first eye, but
  `R_MarkSurfaces`, surface chaining, entity traversal, draw-list construction,
  and most draw submission still occur per eye.
- Each eye renders at the runtime-recommended resolution. `vr_msaa` uses a
  multisampled intermediate followed by an eye-sized resolve. Gamma correction
  and compositor submission also occur per eye. The left eye is additionally
  scaled into the desktop mirror.
- A newer API alone cannot remove two eye-sized depth/color outputs. Shared
  visibility, shared draw-list preparation, instanced or multiview vertex work,
  quality settings, and less expensive passes are the relevant optimizations.

### `mj4m1`

The existing map investigation found that `mj4m1` is an unusually hostile
renderer workload:

- BSP2, approximately 93 MB;
- empty VIS lump;
- approximately 220,000 leaves and 543,000 marksurfaces;
- approximately 428,000 faces and 702 brush models;
- roughly 7,300 map entities.

Earlier desktop profiles remained expensive with `r_drawentities 0`, proving
that the world/brush path is independently important. Entities and enhanced
models then add another significant cost. A valid, tool-generated VIS solution
would be valuable, but the engine should not invent an external BSP2 VIS format
without a producing tool and real content to validate it.

### Ironwail and vkQuake

Ironwail is the primary implementation reference because it shares QuakeSpasm
and OpenGL ancestry. Its relevant features include:

- OpenGL 4.3 renderer;
- compute-based surface marking/culling;
- prebuilt world and brush GPU metadata;
- indirect and multi-draw world submission;
- persistent mapped per-frame upload buffers;
- true alias-model instancing;
- unified GPU pose handling for modern model formats;
- explicit renderer state and pass grouping.

vkQuake remains a design reference for task dependencies, parallel CPU
preparation, explicit resource lifetime, compute skinning/lightmaps, and
indirect rendering. Its Vulkan renderer and desktop swapchain are not a
drop-in backend for this codebase.

## Target architecture and migration rules

### Renderer target

The final target is an OpenGL 4.3 renderer for Windows and Linux with:

- shader-based world, alias, enhanced-model, sprite, particle, water, sky,
  controller, HUD, menu, gamma, and postprocessing paths;
- static GPU geometry and indices wherever content is immutable;
- a fenced ring for per-frame/per-view uploads, using persistent mapping when
  `GL_ARB_buffer_storage` is present and a measured non-stalling fallback when
  it is not;
- explicit render lists and stable material/state keys;
- instanced alias and brush-model submission;
- indirect world draws and GPU-assisted visibility where measurements justify
  them;
- shared stereo frame preparation;
- optional multiview or instanced stereo when supported by the active runtime.

An OpenGL 4.3 **compatibility context** is an acceptable intermediate target.
It allows new fast paths to ship while old fixed-function fallbacks remain.
Moving to a 4.3 core context is a cleanup and correctness milestone, not itself
a performance optimization. Do not request a core context until every active
path used by desktop mode, VR, menus, console, loading, screenshots, particles,
sky/water, controller adjustment, and mod content is shader-based.

The portable OpenGL 4.3 baseline includes VAOs, UBOs, SSBOs, compute shaders,
memory barriers, and indirect drawing. Do not accidentally make later or
vendor extensions baseline requirements:

- buffer storage/persistent mapping is OpenGL 4.4 or
  `GL_ARB_buffer_storage`;
- multi-bind is OpenGL 4.4 or `GL_ARB_multi_bind`;
- bindless textures remain extension/vendor dependent.

Each must have its own capability flag. Persistent mapping needs an ordinary
buffer orphan/map/subdata fallback; multi-bind may fall back to cached
individual binds; bindless textures are optional and must not be needed for
renderer correctness.

### Safety rules

- Keep changes bisectable and guarded by capability checks or temporary cvars.
- Retain the existing renderer as a fallback until a phase's parity matrix
  passes on both Windows and Linux.
- Never silently change visual behavior to obtain a benchmark win. Record
  intentional differences such as MSAA, render scale, shadows, particle
  density, portal culling, skyroom, and enhanced-model selection.
- Treat stereo culling as correctness-sensitive. A shared result must be the
  conservative union of both eye frusta, not the result of reusing one eye.
- Preserve Quake-facing VR behavior: weapon transforms, world-space menus,
  HUD, controller calibration, room-scale movement, network poses, mirror,
  screenshots, and desktop `-novr` mode.
- Profile release builds. Debug contexts and validation must be separate from
  performance captures.
- Do not optimize only `mj4m1`; it is the stress case, not the only acceptance
  workload.

### Capability and fallback policy during development

Use three temporary renderer modes:

- `legacy`: current automatically selected compatibility renderer;
- `gl43`: request OpenGL 4.3 and enable only completed modern paths;
- `auto`: use `gl43` when its current phase is supported and validated,
  otherwise report the reason and use `legacy`.

The exact user-facing cvar/argument names should be selected during Phase 1.
They must not collide with existing archived settings. Every fallback must log
the missing version, extension, shader, framebuffer, or runtime capability.

The final Phase 5 decision may remove `legacy` if Windows/Linux coverage is
good and maintaining it would obstruct renderer development. That removal is
not implicitly authorized by this plan.

## Performance test matrix

Every phase must use the same controlled matrix where applicable.

### Representative content

- `id1`: one indoor and one outdoor/water-heavy map;
- Mjolnir: hub plus `mj4m1` at saved repeatable viewpoints;
- Arcane Dimensions: a high-entity map;
- Dwell: particles/weather and modern effects;
- QBJ3 or another replacement-heavy workload;
- one map with skyroom/portal behavior;
- menus, disconnected menu, loading screen, console, death/reload, and map
  transition.

### Rendering variants

- desktop and VR;
- classic models and enhanced MD3/MD5 models;
- `vr_msaa 0`, `2`, and `4` at fixed eye resolution;
- entities on and off for world isolation;
- particles at normal and reduced density;
- portal culling, shadows, skyroom, water and mirror controls explicitly
  recorded;
- release build after shader/cache warmup.

### Platforms

- Windows x64 native Visual Studio build;
- Linux x86-64 native build;
- at least one NVIDIA and one AMD GPU before a new renderer becomes default;
- SteamVR/OpenVR and the project's Monado/OpenComposite/xrizer route for VR;
- additional Intel testing when hardware is available, but it is not a gate
  unless Intel becomes an announced support target.

### Metrics

Capture at least:

- median, 95th percentile, 99th percentile, and worst non-loading frame time;
- application CPU frame time and GPU frame time;
- first-eye and second-eye setup/mark/draw time;
- compositor missed/reprojected frame count;
- world, alias, MD3, MD5, brush, particle and postprocess draw counts;
- visible/scanned leaves, marksurfaces, unique/chained surfaces, visedicts and
  material/lightmap batch count;
- triangle/vertex counts by geometry class;
- dynamic upload bytes and number of buffer wraps/waits;
- eye target dimensions, samples, formats and mirror cost;
- shader compilation and resource creation stalls, reported separately from
  steady state.

Each benchmark artifact must record commit, executable hash, GPU/driver,
runtime, headset refresh rate, settings, map, viewpoint, and capture duration.
Steady-state captures should contain at least 300 non-loading frames after
warmup. Runtime/compositor timing is required where the active API exposes it;
an unavailable metric must be marked unavailable rather than approximated.

## Phase 1: Measurement, capability foundation, and reproducible baselines

### Goal

Determine whether each workload is CPU traversal-bound, driver/draw-bound,
vertex-bound, fragment/fill-bound, synchronization-bound, or a mixture. Create
the capability and validation foundation needed by all later phases.

### Work

1. Extend `r_perfdebug` records with frame number, eye index, stereo-frame ID,
   first/last-eye flags, target dimensions, MSAA samples, and renderer mode.
2. Separate frame preparation, visibility, entity collection, world draw,
   brush entities, MDL, MD3, MD5, particles, water/sky, MSAA resolve, gamma,
   mirror, compositor wait, and compositor submit timings.
3. Add asynchronous GPU timer queries with a delayed readback ring. Never
   block the measured frame waiting for a query result.
4. Add draw/state/upload counters at the renderer abstraction points rather
   than scattering unconditional logging into inner vertex loops.
5. Add an explicit profiling command/preset that starts a bounded capture and
   writes a compact machine-readable result plus a human summary. Keep raw
   operational/session telemetry local and avoid recording unrelated user
   state.
6. Add OpenGL version/profile/extension reporting and a centralized capability
   structure. Include at least:
   - instanced drawing and attribute divisors;
   - VAOs;
   - buffer storage and persistent/coherent mapping;
   - SSBOs and buffer range binding;
   - compute shaders and memory barriers;
   - indirect and multi-draw indirect;
   - timer queries;
   - texture storage/arrays and multi-bind;
   - multiview-related extensions when present.
7. Add a development-only OpenGL 4.3 context request and debug callback for
   Windows/Linux. Keep it opt-in in this phase.
8. Create repeatable mj4m1 saves/viewpoint scripts and baseline captures for
   the full matrix.

### Likely files

- `Quake/gl_rmain.c`, `Quake/r_world.c`, `Quake/r_alias.c`;
- `Quake/vr.c`, `Quake/gl_screen.c`;
- `Quake/gl_vidsdl.c`, `Quake/glquake.h`;
- build/test scripts under `scripts/` or `tools/`;
- Windows Visual Studio project only if new source files are introduced.

### Exit gate

- The same release build can produce comparable captures on Windows and Linux.
- GPU timing adds no synchronous stalls in the normal frame path.
- Per-eye totals reconcile with the whole VR frame within documented timing
  boundaries.
- Baselines exist for every required content/variant combination.
- At least three repeated captures at a fixed viewpoint stay within 5% for
  median CPU/GPU time after warmup, or the remaining variance is explained.
- The GL 4.3 development context starts in desktop and VR modes on at least one
  Windows and one Linux system and reports missing capabilities cleanly.

### Expected duration

One to three weeks.

## Phase 2: GPU enhanced models and true alias instancing

### Goal

Eliminate immediate-mode MD3/MD5 rendering and reduce per-entity CPU/driver
submission. This phase directly targets the enhanced-model regression.

### Work

1. Define a unified GPU mesh representation for MDL, MD3, and rerelease MD5:
   immutable indices/UVs/base attributes, pose or bone data, surface/material
   ranges, and bounds.
2. Upload MD3 geometry and pose data once at model load. Decode/interpolate
   positions and normals in a vertex shader.
3. Upload MD5 vertices, weights, indices, and joint poses once. Perform skinning
   in a vertex shader first; use compute skinning only if profiling later shows
   reuse or vertex cost justifies it.
4. Preserve skin selection, alpha-tested surfaces, fullbright textures,
   overbright behavior, fog, model interpolation, viewmodel transforms,
   shadows, outlines, `r_showtris`, player colors where applicable, and VR
   weapon tint/calibration.
5. Introduce per-instance data containing model matrix, pose indices/blend,
   light color, alpha, skin/material key, and required flags.
6. Sort opaque alias entities by a stable batch key and issue instanced indexed
   draws for compatible entities. Preserve back-to-front ordering for
   translucent entities unless an explicitly validated OIT path is introduced
   later.
7. Retain the old MD3/MD5 renderer behind a temporary diagnostic fallback
   until visual and performance parity passes.
8. Add model-format-specific metrics: entities, surfaces, instances, draws,
   triangles, uploaded bytes, and batch flush reasons.

### Implementation references

- Current optimized MDL path: `Quake/r_alias.c`.
- Current MD3/MD5 loaders and mesh packing: `Quake/gl_model.c`,
  `Quake/gl_mesh.c`, `Quake/gl_model.h`.
- Ironwail instance and unified pose design: sibling
  `ironwail/Quake/r_alias.c`, `gl_mesh.c`, and shader sources.
- vkQuake compute skinning is a later design reference, not the first port.

### Exit gate

- No enabled MD3/MD5 path falls back to `glBegin`/`glEnd`, including normal,
  alpha, outline, shadow, `r_showtris`, no-cull, viewmodel, and VR weapon-menu
  rendering. The diagnostic legacy renderer is a separately selected path.
- Enhanced models pass image comparisons and interactive checks for opaque,
  alpha-tested, translucent, fullbright, overbright, fogged, shadowed,
  outlined, viewmodel, and VR weapon cases.
- No model-selection, skin, animation, interpolation, calibration, or
  replacement-loading regression across the representative mods.
- On an enhanced-model-heavy scene, CPU model-render time falls by at least
  40% or draw calls fall by at least 50%, with no statistically significant
  GPU regression at identical output settings. If not, profiling identifies a
  different remaining limiter before the phase is expanded.
- The old enhanced-model path can be disabled by default after Windows/Linux
  and both VR runtime routes pass.

### Expected duration

Two to five weeks.

## Phase 3: Shared stereo preparation and render-list architecture

### Goal

Stop repeating view-independent CPU work for each eye. Build explicit frame and
render lists that Phase 4 can consume without classic mutable texture chains.

### Work

1. Split renderer state into three scopes:
   - once per game frame;
   - once per stereo pair;
   - once per eye/view.
2. Locate both eye views before scene preparation and construct a conservative
   union frustum. If the eyes resolve to different leaves or FatPVS inputs, OR
   both visibility sets. Preserve per-eye projection and final rejection where
   useful.
3. Run shared candidate leaf/surface discovery, efrag/entity collection,
   lightstyle/dynamic-light preparation, animation selection, and material
   bucketing once when the result is view-independent or conservatively shared.
4. Create explicit opaque world, brush, alias, sprite, particle, water/sky,
   alpha, overlay, and viewmodel lists. Make ownership and lifetime a stereo
   frame property rather than relying on implicit mutable globals.
5. Share opaque candidate lists and stable material/model grouping. Sort
   translucent surfaces/entities for each eye from the shared candidates unless
   a later validated transparency method makes ordering view-independent. Do
   not use the center eye as the sole authority for visibility or alpha order.
6. Reuse the Phase 2 alias instance list across both eyes while updating only
   per-eye global matrices/eye position.
7. Add a diagnostic mode that compares shared visibility against the union of
   the old two independent eye results and reports any missing surface/entity.
8. Keep mono desktop and non-VR stereo behavior correct; do not make the
   renderer assume exactly two views internally where a small view array is
   practical.

### Likely files

- `Quake/vr.c`, `Quake/gl_rmain.c`, `Quake/r_world.c`;
- `Quake/r_alias.c`, `Quake/r_brush.c`, particle/sky/water entry points;
- `Quake/glquake.h` and a new render-list module if that keeps ownership clear.

### Exit gate

- Diagnostic comparison finds no surface or entity visible to either legacy
  eye that is missing from the shared result across the representative maps.
- Doorways, mirrors/portals if supported, near walls, water boundaries,
  skyrooms, large FOVs, room-scale head motion, alpha surfaces, sprites,
  particles, HUD, viewmodel, weapon wheel, and controller-adjustment visuals
  remain stereo-correct.
- The second eye performs no BSP/PVS leaf walk or entity recollection in the
  normal shared path.
- In CPU-bound VR scenes, combined stereo setup/visibility CPU time falls by at
  least 30%. A lower result is acceptable only if counters prove the shared
  work is already a small portion of the frame.
- Desktop frame time and behavior do not regress materially.

### Expected duration

Two to five weeks.

## Phase 4: OpenGL 4.3 world renderer and frame resources

### Goal

Move the dominant world/brush pipeline from CPU texture chains and client
submission to prebuilt GPU data, fenced frame resources, indirect draws, and
GPU-assisted marking. Make the GL 4.3 path feature-complete on Windows and
Linux while retaining the legacy path for comparison.

### Work package A: context, functions, and state

1. Complete the OpenGL 4.3 function loader and strict capability validation.
2. Add VAO ownership and explicit vertex layouts.
3. Replace ad hoc global GL state changes with a cached state description for
   depth, blend, cull, polygon, color mask, framebuffer, pipeline, textures,
   buffers, and vertex layout.
4. Add shader compilation diagnostics and stable permutation keys. Compile and
   cache required permutations during controlled initialization rather than
   first combat use.
5. Add a fenced frame-resource ring for uniforms, SSBOs, vertices, indices,
   instance data, and indirect commands. Use persistent mapping only when
   `GL_ARB_buffer_storage` is available; implement and measure a safe ordinary
   buffer fallback. Instrument waits, wraps, high-water marks, and fallback
   uploads.

### Work package B: world and brush preprocessing

1. Build immutable combined world/brush vertex and index buffers at map load.
2. Precompute surface metadata, material/lightmap identifiers, draw ranges,
   BSP/leaf/marksurface references, and brush-model instance data.
3. Compute the required CPU and GPU allocation sizes before committing map
   resources. Enforce overflow checks and a documented budget/failure policy;
   on an unsupported allocation, report the sizes and fall back cleanly rather
   than partially constructing the GL 4.3 world.
4. Replace CPU client-index batches with GPU index ranges.
5. Group draws by explicit material/lightmap/state keys without changing
   alpha, fence, water, sky, missing-texture, fullbright, fog, overbright,
   dynamic lightmap, or external texture behavior.
6. Add instanced brush-model rendering where transformation/material rules are
   compatible.

### Work package C: indirect drawing and visibility

1. First produce indirect commands on the CPU into the frame-resource ring.
   This validates rendering and data structures before compute is involved.
2. Add multi-draw indirect by material/state range.
3. Port an Ironwail-style compute marking path behind a cvar:
   - upload PVS/frustum/view data;
   - clear and populate visibility/indirect metadata;
   - issue explicit memory barriers;
   - gather compact draw commands if needed.
4. Retain a CPU visibility fallback using the same prebuilt GPU draw metadata.
   This separates compute-culling failures from the new drawing path.
5. Preserve dynamic lightmap correctness. Move updates to persistent uploads
   or compute only after the static world path is stable.

### Work package D: remaining compatibility paths

Convert remaining active fixed-function features needed for GL 4.3 core
eligibility, including:

- 2D drawing, console, menus and loading;
- sky, water/warp and sprites;
- classic and scripted particles, beams and decals;
- controller render models and adjustment lines;
- debug geometry, bounding boxes and show-tris;
- polyblend, gamma/postprocess, mirror and screenshots.

This package may use a 4.3 compatibility context while conversions land. Do
not claim core-profile readiness until a source scan and runtime debug context
show that no removed API is called.

### Exit gate

- `gl43` renders every representative desktop and VR case with visual parity.
- The world fast path uses GPU indices and indirect draws; it does not construct
  per-frame client-memory triangle-index batches.
- The compute visibility result matches the CPU fallback in validation mode.
- Buffer-ring waits are rare and quantified; no resource is overwritten while
  referenced by the GPU.
- mj4m1 map-load time, peak CPU allocation, and GPU allocation are recorded;
  budget/capability failure falls back or exits cleanly without an OOM, partial
  world, or renderer crash.
- No OpenGL debug errors occur in the validation matrix.
- On `mj4m1` with entities disabled, 95th-percentile CPU world/setup time falls
  by at least 50% against the Phase 1 baseline. If GPU fill becomes dominant,
  that transition is documented with GPU timings.
- With normal entities, Phase 2/3/4 together materially improve missed-frame
  behavior at identical eye resolution and MSAA settings.
- Windows and Linux builds and VR submission survive video restart, map change,
  disconnect/menu, suspend/focus loss where applicable, and clean shutdown.

### Expected duration

One to three engineer-months. Land work packages separately.

## Phase 5: Stereo GPU optimization and GL 4.3 default transition

### Goal

Reduce duplicated GPU command and vertex work after the renderer is fully
shader-driven, then decide whether OpenGL 4.3 becomes mandatory/default.

### Work

1. Establish the post-Phase-4 bottleneck with GPU timestamps and compositor
   data. Do not implement multiview if fragment shading, MSAA, or unrelated
   passes dominate and the extension path provides no expected benefit.
2. Prototype instanced stereo using a two-layer color/depth target and a
   per-view matrix array. Render compatible geometry once with two instances.
3. Evaluate runtime-compatible multiview extensions where available. Keep the
   generic two-pass renderer as a fallback because OpenVR/OpenComposite/runtime
   behavior may vary.
4. Submit or resolve the final layer(s) into the compositor-required eye
   textures without unnecessary intermediate copies.
5. Add a runtime hidden-area mesh path if the active VR API exposes valid mesh
   data. Apply it before expensive fragment passes.
6. Add explicit mirror modes: off, left eye, right eye, and scaled, measuring
   each. Avoid the mirror blit when disabled.
7. Revisit MSAA defaults using measured headset resolution and temporal
   stability. Any adaptive quality feature must be opt-in until image quality
   and frame pacing are predictable.
8. Consider foveation only through well-supported runtime/API extensions and
   only after the base renderer is stable.
9. Run the full release matrix and select the final policy:
   - make GL 4.3 `auto` the default while retaining legacy fallback;
   - require GL 4.3 but retain two-pass VR fallback;
   - optionally move from a 4.3 compatibility context to a core context after
     proving no removed calls remain.
10. Remove diagnostic fallbacks only in separate, easily reversible commits
    after release soak time.

### Exit gate

- Stereo optimization produces identical eye transforms, IPD, asymmetric FOV,
  depth, culling, alpha order, HUD/menu placement, viewmodel and controller
  behavior.
- The optimized path is exercised on SteamVR and Monado/OpenComposite/xrizer on
  Windows/Linux as applicable.
- It either reduces GPU vertex/command time or total GPU time by a predefined
  useful threshold (initial target 15%) on at least two representative VR
  workloads, or it remains optional and the result is documented.
- Frame pacing and compositor missed-frame counts improve or remain neutral;
  average FPS alone is insufficient.
- The selected GL 4.3 default/fallback policy is documented in release notes,
  launcher checks, error messages, build documentation, and support matrix.
- The final renderer has a clean shutdown/restart path and no debug-context
  errors or resource leaks in repeated map/video/VR cycles.

### Expected duration

Three to eight weeks, depending on runtime extension compatibility and whether
core-profile cleanup is included.

## Separate track: native OpenXR

Native OpenXR should begin only after renderer ownership boundaries are clear,
or proceed in a separate branch with no renderer restructuring. It is not a
sixth performance phase and is not required to reach OpenGL 4.3.

Recommended sequence:

1. Define a private runtime-backend interface behind the existing Quake-facing
   VR API: lifecycle/events, predicted frame/views, hand poses/input snapshot,
   eye target acquisition/release, submission, and haptics.
2. Preserve OpenVR as one backend while adding OpenXR-over-OpenGL.
3. Centralize one OpenXR frame owner with `xrWaitFrame`, `xrBeginFrame`,
   `xrLocateViews`, swapchain acquire/wait/release, and `xrEndFrame`.
4. Port semantic actions and interaction-profile bindings into the existing
   Quake input behavior.
5. Initially use generic controller geometry where no portable runtime model is
   available.
6. Validate session focus/loss, reference-space changes, runtime restart, video
   restart, loading/menu frames, gamma, MSAA, mirror, and haptics.

OpenXR is expected to improve maintainability and remove the current
OpenVR-to-OpenXR translation dependency. It should not be accepted on an
assumption of significant renderer speedup; measure it independently.

## Deferred work

- A full Vulkan renderer port is deferred until after Phase 5 measurements.
- Ray tracing, order-independent transparency, clustered lighting, compute
  skinning, bindless textures, dynamic resolution, foveation, and render-task
  parallelism are optional later projects. They should be justified by current
  profiles, not imported merely because Ironwail or vkQuake implements them.
- Parallel CPU preparation should follow stable explicit render lists. Adding
  threads while renderer state is implicit and mutable would increase risk.

## Delivery and review discipline

Each implementation slice should include:

- one focused behavior/performance objective;
- before/after captures from the controlled matrix;
- new capability/fallback behavior documented;
- Windows and Linux build verification;
- desktop and VR functional checks proportional to the touched path;
- no bundled renderer cleanup unrelated to the slice;
- a reversible cvar or backend fallback until its phase exit gate passes;
- an updated section in this document recording status, benchmark artifacts,
  deviations, and remaining risks.

Do not publish a claimed performance improvement without recording identical
visual settings, eye dimensions, MSAA, runtime, viewpoint, and build type.

## Completion definition

The project has completed this plan when:

- enhanced MD3/MD5 models use GPU-buffered shader rendering;
- compatible alias and brush entities are truly instanced;
- CPU visibility/entity preparation is shared across a stereo pair;
- the world path uses prebuilt GPU data, persistent frame resources, and
  indirect submission with a validated GPU-marking path;
- Windows and Linux use a validated OpenGL 4.3 renderer by default or by an
  explicitly documented automatic policy;
- VR stereo GPU optimization is enabled only where it is correct and useful;
- `mj4m1`, enhanced-model mods, ordinary id1 maps, menus/loading, and desktop
  mode pass the documented matrix;
- performance gains are demonstrated in CPU time, GPU time, percentile frame
  time, and compositor missed-frame behavior rather than anecdotal FPS alone.
