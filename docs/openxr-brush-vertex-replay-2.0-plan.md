# Brush vertex regeneration for device reconstruction

Status: verified pre-production brief, not implemented or source accepted.
This is a prerequisite of [device reconstruction](openxr-device-reconstruction-2.0-plan.md),
not a complete brush/lightmap/device recovery claim. Only2.0 production edits;
primary and runtime game assets remain read-only. No builds/tests until the full
implementation is finished.

## Behavior and verified owners

A loaded map must be able to regenerate the brush vertex/submodel upload on a
new Vulkan device without reloading the game, resetting particles/lightstyles,
changing model/surface identities, or retaining another permanent jumbo-map GPU
payload. The initial upload remains vkQuake's renderer and graphics.

Direct source comparison: donor vkquake4bc898f2 `r_brush.c:1464` constructs
texture/lightmap UVs from BSP vertexes, edges, texture vectors and surface atlas
coordinates; its vertex-buffer builder at2474 copies those polygons. Current2.0
`BuildSurfaceDisplayList:1619` retains the donor arithmetic, while
`GL_BuildBModelVertexBuffer:2629` subsequently frees ordinary polygons. Tiled
polygons are retained for runtime users and repeat map setup. On-device-switch
repeating the builder dereferences absent ordinary polygons at2664. Reusing
R_NewMap would reset live particles, lightstyles and counters, and also rebuild
atlas/draw membership; it is not the narrow GPU replay boundary.

Current2.0 already has checked `Mod_SurfaceVertexPosition` atgl_model.c610,
used by decals/showtris. It reproduces donor signed-edge orientation, including
edgezero, while checking edge/vertex indices. Reuse it rather than add another
BSP traversal. Current surface UV/atlas metadata survives the polygon release.
No evidence requires a second retained brush upload cache or reparsed qmodels.

## Minimal adapter versus alternatives

Extract the donor's existing surface vertex calculation into one owner-local
routine that fills a caller-provided VERTEXSIZE float span from a qmodel/surface.
Preserve operation order, turbulence coordinates, Q64 texture shift, half-texel
lightmap offsets and current atlas assignment. Reuse the checked position helper
for positions. BuildSurfaceDisplayList still owns its initial glpoly allocation
and links, but invokes this same vertex calculation. The existing vertex upload
copies retained polygons as before; when an ordinary polygon is absent, fill
the same destination directly using that routine. Never allocate replacement
polygons merely to free them again. Keep original VBO ordering, vbo_firstvert,
submodel tagging, usage flags, staging, descriptors and GPU creation calls.

Alternatives rejected: retaining all ordinary polygons/upload arrays loses the
existing heap reduction; rebuilding the map resets running state; a second
vertex/UV implementation duplicates precision and texture policy. This shared
calculation introduces no model registry, replay state machine or GPU cache.
The preserved tiled path avoids changing sky/unlit-water source policy.

Input failure must be known before creating GPU buffers. Use the existing
checked BSP position traversal and verify required texinfo/texture/dimensions,
surface edge count and destination range before writes. A bounded all-model
read-only eligibility helper may feed the parent transaction's preflight;
it must traverse the same existing client precache, skip inline shared owners,
and use checked count/range arithmetic. It cannot mutate model/surface state
or authorize the whole device transaction. Reuse a single validation predicate
instead of a parallel manifest. Keep the existing initial-map error behavior
for invalid assets, with explicit failure before partial buffer creation.

Write set: r_brush.c and bounded declarations in its existing public header if
preflight is exposed. Estimated120–240 changed lines, including moved unchanged
donor arithmetic. Source review must distinguish moves from new policy and
challenge whether the public helper is necessary. No full lightmap replay,
BLAS retirement, GPU wait/lost-device policy or device-switch activation here.
Those remain under the parent plan with their own owner contracts.

Smallest end-of-full-implementation proof: compare initial and regenerated
vertex/submodel uploads for retained tiled and freed ordinary polygons, including
lightmapped liquids, Q64 shifts and movable/external brush models. Check loaded
model/surface identity, existing atlas coordinates, native draw ordering,
particles and live lightstyles remain intact. Validate malformed input before
GPU creation and a loaded scene after complete parent reconstruction. Linux/ARM
qualification is at the end; actual headset/performance trials are user-owned.

Local Astra must verify and dispose this brief before production changes.

## Local Astra design disposition

Sartre personally verified donor/current arithmetic and retained BSP metadata.
Accepted with one P2 validation clarification; no P1 or architecture reopening.
Adopt: eligibility covers both upload branches. Retained polygon declarations
must provide enough vertices for numedges; missing tiled polygons fail rather
than use ordinary regeneration. Absent ordinary polygons require nonnegative
BSP counts and nonnull edge/vertex arrays before the checked position helper.
Checked aggregate bytes and per-surface destination ranges apply to both;
world-submodel count/pointer/tagging inputs also need validation.

One thin public boolean eligibility function is accepted because the parent
transaction needs its answer before destructive retirement. It uses the same
private validation as the builder, local counts and existing precache traversal,
without mutating CPU/GPU state. Vertex calculation remains private. Preserve
donor operation order and original initial polygon ownership/tiled copying.
The estimate is plausible but tight; source review must separate moved arithmetic
from new checks and challenge duplicated policy, not just line count. Full
lightmap/BLAS/device recovery and qualification are still outside this slice.
