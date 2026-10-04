# CAND-PERF-002 and CAND-PERF-001: world shader additions

Status: design submitted for Astra xhigh review before renderer implementation.
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
stereo_clip_from_center[eye] * center view_projection_matrix, matching emitted clip.
Viewport offset/extent, Vulkan 0..1 depth and any z-fix displacement require explicit
handling. Plane normal can derive from world-position derivatives before divergent
light iteration. Tiny/degenerate derivatives need a safe non-NaN path.

Compute projection must use the same matrices and conservative screen tile padding
for the maximum enabled VRS rate; cluster boundaries may not lose lights when a
coarse fragment represents several pixels. Reject nonfinite/nonpositive light ranges.

## Scheduling and existing graphics

R_UpdateLightmapsAndIndirect already depends on deferred particle light flush,
entity/alpha recording and visible surface culling. Reuse its command owner to upload
lights and dispatch clustering; frame matrix/header publication occurs after matrix
setup and before recording. Preserve primary command submission order and add
HOST_WRITE/COMPUTE_WRITE -> COMPUTE_READ/FRAGMENT_READ barriers. No new background
render queue or lighting state machine. Handle tasks-on and tasks-off paths.

Cluster-enabled compatible frames update static lightstyles through native lightmap
compute while omitting dynamic-light additions there, to avoid double lighting and
remove the work the feature targets. Switching modes must clear previously cached
dynamic contributions once and rebuild the current native contributions when returning.
Alias/model lighting stays native. r_dynamic=0 suppresses both dynamic modes.

Port donor fragment plane-distance light contribution with target KEX/minlight/cone
semantics retained. Static light scaling/fullbrights/fog/alpha remain vkQuake.
RT shadow mode remains native lightmap mode explicitly: r_rtshadows>0 selects
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
