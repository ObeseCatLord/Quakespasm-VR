# CAND-PERF-002 and CAND-PERF-001: world shader additions

Status: Astra xhigh reviewed; concrete fixes below adopted before implementation.
Donor Ironwail 08d578136ff43d7d1ef38e636dfbfd3e844be7cd.

## Minimal adapter versus replacement

Retain vkQuake world, lightstyle/lightmap, alias lighting, PVS, Vulkan command/task,
transparency, MSAA and OpenXR multiview owners. No new renderer or light truth.
Clustered fragment lighting is an explicit optional world-light evaluation mode
(default native vkQuake), not an unmeasured promised speedup. Canonical cl_dlights
still owns lifetime, radius, colors, minlight, KEX intensity and spotlight fields.

Existing world set4 contains vertex-submodel indices(binding0) and instances(binding1).
Set5 is ALREADY the OpenXR stereo UBO. Extend set4 narrowly with binding2 storage
buffer for frame lighting header, immutable light snapshot and fixed cluster masks.
This preserves the current 5 desktop/6 stereo descriptor-set counts, existing 88-byte
push constants and instance layout. It must be bound for every world/brush path.

Ironwail uses 32x16x32 clusters and two uint32 masks for 64 lights. Port its conservative
sphere/cluster AABB calculation and bit iteration, adapted to Vulkan coordinates,
reverse-Z and per-eye asymmetric/canted projections. Use fixed two-eye capacity,
not separate light owners or mono visibility. Compute builds masks into the existing
lighting storage allocation using a minimal compute pipeline before scene execution.
No GPU readback or per-frame wait-idle. Existing frame-slot fences protect buffers.

The existing world vertex shader is not universally world-position aware: ordinary
non-instanced brush mvp includes its entity transform. Avoid adding a model matrix
(push constants would exceed portable 128 bytes). Reconstruct actual world position
from gl_FragCoord and inverse actual per-eye view-projection in lighting header.
Desktop uses inverse native view_projection_matrix. Multiview uses inverse
actual published scene_clip[eye] * center view_projection_matrix, including underwater deformation and eye-specific scene overrides.
Viewport offset/extent, Vulkan 0..1 depth and any z-fix displacement require explicit
handling. Plane normal can derive from world-position derivatives before divergent
light iteration. Tiny/degenerate derivatives need a safe non-NaN path.

Compute projection must use the same matrices and conservative screen tile padding
for the maximum enabled VRS rate; cluster boundaries may not lose lights when a
coarse fragment represents several pixels. Reject nonfinite/nonpositive light ranges.

Depth is positive eye-forward **plane** depth, never radial distance. Publish
`eye_forward[eye] = normalize(unproject(actual_inverse_VP, vec3(0,0,1)) - true_eye)`
using reversed Vulkan near Z=1. For each padded tile corner, compute
`ray = unproject(vec3(corner_xy,1)) - true_eye` and `plane_ray = ray / dot(ray,eye_forward)`.
At slice endpoints the corners are `true_eye + plane_ray * depth`. Their eight-point
AABB encloses a linear truncated pyramid, including interior extrema; normalized
rays at radial distances would instead make spherical patches and are rejected.
Fragment indexing uses the same `dot(world_pos-true_eye,eye_forward)` convention.
Slice zero begins at depth zero, keeping geometry before the nominal logarithmic
near parameter. Slice 31 is unbounded and uses **all active light bits**, without
an arbitrary far cap. If padding crosses a nonpositive/invalid ray-plane denominator,
that tile also keeps all active lights instead of constructing an invalid AABB.


## Scheduling and existing graphics

R_UpdateLightmapsAndIndirect already depends on deferred particle light flush,
entity/alpha recording and visible surface culling. Reuse its command owner to upload
lights and dispatch clustering; frame matrix/header publication occurs after matrix
setup and before recording. Preserve primary command submission order and add
HOST_WRITE/COMPUTE_WRITE -> COMPUTE_READ/FRAGMENT_READ barriers. No new background
render queue or lighting state machine. Handle tasks-on and tasks-off paths.

Cluster-enabled compatible frames update static lightstyles through native lightmap
compute while omitting dynamic-light additions there, to avoid double lighting and
remove the work the feature targets. Switching modes forces unconditional atlas-region regeneration including invisible and moved brushes, clearing cached dynamic contributions and rebuilding native contributions when returning.
Alias/model lighting stays native. r_dynamic=0 suppresses both dynamic modes.
Forced rebuilds refresh the current submodel transforms even when atlas light counts
are zero. In `update_lightmap.inc`, static-only evaluation never enters the dlight
cull/world-position branches that read these transforms; refreshing on transitions
also makes moved/hidden-brush native re-entry independent of that optimization.


Reuse target update_lightmap.inc Euclidean/minlight/KEX/cone equations at reconstructed fragment positions; donor plane/minlight-fade/saturation equations are deliberately not imported. Static light scaling/fullbrights/fog/alpha remain vkQuake.
The native compute stores dynamic contributions unchanged in 8-bit mode and scaled
by .25 in 10-bit mode. World sampling multiplies by 2 or 8 respectively, so the
fragment contribution is always multiplied by **2**, independent of atlas format.
Native surface normals are BSP plane normals flipped for `SURF_PLANEBACK`; the
KEX Lambert term uses that outward, eye-facing side. Derivative normals are oriented
by their dot product with the actual `eye_origin-world_pos` to match that convention
across the Vulkan projection Y flip and canted/transformed views, before discard.

RT shadow or CPU lightmap mode remains native explicitly: r_rtshadows>0 or r_gpulightmapupdate=0 selects
native effective lighting and the UI explains that cluster mode requires RT shadows
off. Do not silently discard shadows. This is a visible compatibility rule rather
than a second shadow implementation. Check non-instanced/instanced/moving/cutout
brushes, WBOIT/MBOIT/MSAA variants and eye-specific scene passes.

## Surface dithering

Reuse Ironwail triangular whitenoise seeded by surface lightmap coordinates. Add
optional r_surface_dither intensity (default0 preserves vkQuake default graphics)
to the same frame header; world shader uses anchored noise, luma scaling and
derivative-based distance fade. Identical surface UVs produce identical eye noise;
no eye/frame/screen seed. Apply after fog/fullbright with bounded linear/sqrt-space
formula; retain native postprocessing palette choices and existing screen blue noise.
This adds surface-anchored quality control, not a claim to replace all palette noise.

## Write scope and final proof

Renderer worker owns r_brush.c, gl_rmain.c, gl_rmisc.c, glquake.h, existing world
shader family/common plus new clustered includes/compute, new fixture and plan.
Main owns build registration and graphics menu controls; report exact hooks.
No modifications to base SSAO, OpenXR runtime, gameplay or task pool.

Tests run only after all selected feature implementation is finished. Required
shader variant compilation and actual GPU buffer/fragment checks: two-eye asymmetric
frusta, translating/rotating/non-instanced brushes, VRS tile edges, 0/64 lights,
KEX/cones, RT native fallback, cached mode changes, translucency/fullbrights and
tasks on/off. User benchmarks later; leave native default until measured.

## Astra review disposition (verified gpt-6-astra/xhigh)

| Finding | Disposition | Required adaptation |
| --- | --- | --- |
| Scene clip differs under water | Adopt | Use actual publication/overrides; inverse actual eye VP |
| Clip-space z-fix | Adopt | Flag from existing instance_base word; restore NDC Z by gl_FragCoord.w/1024 before unproject |
| Ironwail/native light formula differs | Adapt | Port clustering only; reuse native Euclidean/minlight/KEX/cone contribution in native pre-scaling units |
| Atlas clearing skipped if unmodified | Adopt | Force unconditional full affected regions on effective-mode transition, including hidden/moving brushes |
| CPU updater duplicates lighting | Adopt | Effective native fallback if GPU updater disabled; decision latched once per frame |
| Slot flips/shared descriptor hazard | Adopt | Immutable per-frame-slot set4 descriptors; latch slot before recording; publish after fence and flush noncoherent mapped memory |
| Cluster projection/far tail | Adapt | Eight-corner conservative bounds in actual eye convention, positive depth slices with covered terminal tail; VRS and density padding |
| Dither quantization/discard derivatives | Adopt | Quantize seed independently of lightmap sample; derivatives before discard, intensity0 bypass, preserve overbright and alpha |

Submission/fence order must be verified against gl_vidsdl.c before implementing GPU
resource reuse; that file was outside the senior review scope. The compute slot
index that flips inside the lightmap updater must not select world fragment data.

## Renderer integration handoff

The build owner must compile `Shaders/cluster_lights.comp` as
`cluster_lights_comp` in both Make and Meson with target-env-vulkan1.1, and declare
its generated `cluster_lights_comp_spv`/`_size` pair in `Shaders/shaders.h`. The
renderer owns the corresponding `gl_rmisc.c` module lifecycle and creates the
cluster pipeline after the update-lightmap pipelines. The graphics owner exposes
only `r_clustered_lights` and `r_surface_dither`; effective mode remains
renderer-latched and falls back to native when RT shadows, CPU lightmap updates, or
dynamic lights are disabled.
UI reads `R_ClusteredLightingStatus()`; it performs no independent compatibility
predicate evaluation. An atomic enum from the same frame-header effective decision
selects immutable strings for not-evaluated, off, RT, CPU lightmaps, dynamics off,
pipeline unavailable, cheat and invalid-matrix cases; active returns NULL.
`VID_GraphicsAASampleMask()` is declared in glquake.h and implemented by the main
owner in gl_vidsdl.c.


### Frame-slot submission verification

Read-only inspection of `GL_BeginRenderingTask` confirms it waits and resets the
current command-buffer fence before that frame slot's mapped allocations are reused.
`GL_EndRenderingTask` calls `R_FlushDynamicBuffers` before `R_RecordFrame`; the
flush includes `frame_upload_buffers_memory`. `R_DescribeFrame` emits
`PCBX_UPDATE_LIGHTMAPS` before render passes in the one queue submission. The
cluster header therefore uses an immutable descriptor set for the fenced slot, a
mapped-memory flush, and host→compute/fragment plus compute→fragment barriers; it
does not rely on a host barrier alone. CPU-lightmap frames also advance the header
slot after publishing their disabled decision; otherwise consecutive CPU frames
could overwrite the same header while its preceding submission still reads it.

### Binding 2 ABI and deferred coverage

| Field | Byte offset | Array stride |
| --- | ---: | ---: |
| inverse_view_projection[2] | 0 | 64 |
| eye_origin[2] | 128 | 16 |
| eye_forward[2] | 160 | 16 |
| viewport | 192 | — |
| params | 208 | — |
| counts | 224 | — |
| lights[64] | 240 | 48 |
| masks[2][16384] | 3312 | 8 per mask, 131072 per eye |

Payload size is 265456 bytes. Main review caught that this is not necessarily
a valid second-slot descriptor offset. Allocation, mapped-pointer selection,
descriptor offsets and barriers all use a separate stride rounded up to the
device's `minStorageBufferOffsetAlignment`; descriptor range remains payload
size. This preserves the GLSL ABI without assuming a particular GPU alignment.

Main also preserves the native UNORM lightmap storage range after fragment
lighting: combined static/dynamic light is clamped to 0..2 (normal atlas) or
0..8 (scaled atlas), before diffuse/fullbright/fog composition. The default-off
path bypasses this new calculation entirely. Spatial sampling differs from
atlas baking, so the bounded native/cluster desktop captures pass visual inspection; the full visual matrix remains additional qualification.

The CPU frame struct and GLSL std430 block span 265456 bytes. CPU offset/size
assertions document the contract; shader reflection and uploaded-buffer validation
passed the final production shader reflection and GPU dispatch/readback check.

`tests/cluster_lighting_geometry_fixture.c` is an independent CPU numerical
reference, not production GLSL execution. It supplies known synthetic inverse
reverse-Z matrices, checks the old backwards ray and central spherical-boundary
counterexamples, exercises translated/canted/asymmetric pyramids with interior
samples, matches forward-depth slice selection, and checks first-slice/padded-tile
and 0/1/31/32/33/63/64-light terminal masks. It also checks native 8/10-bit lighting
scale and eye-facing KEX normal expectations. These numerical checks cannot prove
GLSL transcription, descriptor ABI, synchronization, or pixels. The graphics
fixture requires actual compute-mask inspection and rendering against those same
witnesses. The final implementation batch has now passed CPU geometry, production GPU compute, strict shader compilation, and ten live renderer cases in desktop and two-eye simulated OpenXR. See `tests/cluster_lighting_graphics_cases.md` for exact evidence and limits.
