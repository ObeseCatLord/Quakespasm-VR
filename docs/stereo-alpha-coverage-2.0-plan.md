# Native stereo alpha coverage continuation

2026-10-02. Existing F05 only, following local Astra dispositions. Current
center/eye clip matrices independently project the fixed MDL XZ planes onto
observed native mirror regions, including opposite/reversed arrangements.
Current visible-input and successful native presentation receipts stay accepted;
composition/source-footprint proof remains open. First try certifying existing
captures before changing geometry or resolution.

Minimal adapter: add one private test-fixture console command to existing
stereo_alpha_native_fixture.c, which already includes the whole native cl_parse
owner. Native command scheduling writes a bounded geometry JSON using loaded
cl.worldmodel water_surfs/polys/planes and the two parsed static alias identities,
origins/angles/scales. No copied BSP loader, renderer/projection owner, PVS/mask/
list injection or production command. Reuse actual native glpoly xyz used by the
renderer and current half-alpha parsed aliases. Main reads alongside existing
captured native center/eye matrices and installed MDL manifest/bytes. The
command is queued after initialization, before pause/capture, avoiding blocking
model/file/GPU calls from debugger. Read-only loaded geometry; no GPU decision.

Writer contract: fixture_alpha_geometry OUTPUT_NEW_FILE, main thread, signon4,
known stock world, exactly2 fixture statics with alias models and zero angles.
Exclusive fopen wx prevents replacing existing content. Assert native water
surface indices and counts, bound<=1024surfaces/256polys each/256vertices each,
reject cycles or invalid pointers/indices/counts rather than truncate. Output
schema1, world name,2entity records(name/index/origin/angles/scale/scale_origin),
water surface index/flags/plane_normal/plane_dist and polygons of xyz triples.
Use round-trip float precision, check every output/ferror/fclose, no geometry
admission/rewrite. Fixture binary owns only replacement cl_parse object as before.
No general export framework or new required product feature (imagedump remains
excluded). New fixture data is private licensed-stock metadata, not committed.

Main owns native compile/relink, GDB command injection and actual controlled GPU
run/analysis, docs/checker, transport/error budget design and integration.
Luna/xhigh owns only that C fixture addition/registration, no GPU/build/commit.
Nonoverlapping writes, preserve concurrent work. Return<=300word bounded scope,
checks/assumptions/risks/followup; missing evidence reports instead broadening.

Coverage checker will independently reconstruct model/world clip projection,
map each mirror pixel's native sampling footprint, exclude MSAA/bilinear/geometry/
UI/category boundaries, and require author-color reconstruction from B/E alone.
Water polygon full-coverage plus B/W influence and distinguishability against
wrong-order/missing-water/missing-entity/background precede C evaluation.
Numerical/transfer budget is an independent unresolved decision: no fitted
current residual, no universal bit-depth-only bound. Current same-side witness
sets may still be inconclusive. A changed test plane or1:1mirror requires a
separate exact before-code input plan; native Vulkan remains unchanged.
