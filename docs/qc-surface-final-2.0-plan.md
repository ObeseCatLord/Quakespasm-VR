# Loaded native BSP surface qualification

2026-10-01. Before-code plan for finite F03 surface family. GPU remains stopped.
Main verified current pr_ext.c1988–2400/7128, native gl_model.c face/submodel
setup and actual host.c986/1033 against readonly primary pr_cmds.c3416–3722.
Earlier [source plan/repairs](qc-inline-surface-2.0-plan.md) preserve native owners.
No additional production defect established; do not replace surface services,
cache/clipping/model loader or native lightmap formulas to make tests simpler.

Reuse existing licensed-program assembler and dedicated native fixture. Add
separate --surfaces / -surfaces mode, assemble trailing surfaces=False. Original
six prefixes/version/CRC/entity width unchanged; previous modes/variants identical.
Append nine native query callers: numpoints434, point435, normal436, texture437,
nearpoint438, clippedpoint439, attribute486, numtriangles628, triangle629; plus
native strlen114, normalize9, strcat115 return primers. No-op CSQC_Ent_Update
admits actual native client loading. Exact ref globals fixture_ref_surface_NAME
for NAME numpoints/point/normal/texture/nearpoint/clippedpoint/attribute/
numtriangles/triangle. Inputs fixture_surface_input_entity(entity), surface(float),
index(float), attribute(float), point(vector). Outputs fixture_surface_scalar(float),
vector(vector), string(string), prime_scalar(float), prime_vector(vector),
prime_string(string). Each target caller primes through native strlen of
surface-prime (length13), normalize(1,2,3), or strcat(surface-prime), records the
actual nonzero result and then captures target. Do not assign OFS_RETURN in C.

Native C fixture under QC_SURFACE_NATIVE_HOST_FIXTURE reuses host adapter for
CL_LoadCSProgs. Add a10line test-only pr_ext-owner adapter including unchanged
native pr_ext.c, exposing only FixtureSurfaceCacheValid/Count read access to its
static cache. Replace only native pr_ext object in this fixture link. No copied
cache/clipping or production API. Main prepares real model/edict inputs; all
target results come through loaded QC/native numeric handlers.

For actual e1m1 world and a loaded inline model (positive firstmodelsurface),
walk real signed surfedges/edges/vertices independently for expected point.
Check actual edge counts, triangle counts/fan0,index+1,index+2, normals/back flag,
texture name and attribute0–6; valid last vertex/triangle and first invalid.
Attrs: position; normalized texinfo axis perpendicular to plane; oriented normal;
texture-axis coordinates/dimensions; **native** texturemins/light_s+.5 atlas
formula; white1. Dedicated loader uses nonzero allocated surface memory and skips
CalcSurfaceExtents as well as rendered atlas allocation: byte-save texturemins[2]
and light_s/light_t, set texturemins16/32 and atlas7/11 as prepared metadata for
attribute5, restore bytes afterward. Do not certify GPU atlas output from this.

Inline count-bound witness: temporarily alter the unrelated relative world face
numedges to3 then5 while actual offset inline face has4edges. Query valid vertex3/
triangle1 under smaller relative count; invalid vertex4/triangle2 under larger
relative count. Restore before other geometry queries. Degenerate actual face
numedges2 must refuse triangle count/triangle without underflow; restore. These
are prepared metadata inputs over real geometry, not malformed BSP loader proof.
If no real four-edge inline face exists, report unavailable rather than fabricating
another model or claiming this witness passed.

Invalid indexed wrappers: relative index=count or count+7; prepared model base-1,
base=numsurfaces, base=numsurfaces-1 with count2/index1, count0; restore afterward.
Require scalar0/string raw0/vectorXYZ0 after native primers; clipped invalid
returns input point. Negative index only clippedpoint's signed input; no invented
unsigned-float malformed policy. Missing texinfo/texture for texture-name query
refuses; with valid texinfo but absent texture attr4 dimensions1; restore pointers.
Invalid entity modelindex0 refuses; no invalid raw entity reference test.

Projection: actual convex face vertex centroid, input centroid+normal*2, clipped
returns centroid within .02 and on plane; invalid clipped preserves nonzero input.
Nearest: select axis-aligned valid world face centroid (plane residual<.001), query
on-plane point -> native selected face at zero distance. Observe native cache
valid/count positive, repeat same query/output/count, then far point(1048576 each)
->-1. This witnesses cache state/repeated output, not a branch-hit/performance
counter. Do not call nearest with corrupted metadata; indexed guards and loader
validation are distinct. Native loaders reset cache before queries in each VM.

Both VMs run world/inline checks. Prepare cl.worldmodel and copy native sv.models
to cl.model_precache as borrowed actual model inputs, invoke native loader, never
assign GetModel/hook or allocate a fake VM. Two client loads/clears preserve server
program; native server replacement reruns probes, no old model pointer afterward.
No authored mod/HUD/renderer/lit atlas, all malformed geometry, ARM/Windows/hardware
or full F03/F10 claim. Remaining entity-copy/player/command/error owners stay open.

One Luna/xhigh source-only worker owns existing generator/native fixture plus
tests/qc_surface_owner_fixture.c; <=140 Python/<=330 C/<=12 adapter added lines.
All three files tightly coupled; main owns docs/private profiles and integration.
Worker not alone, no delegation/production/docs/branch/commit/runtime/builds/tests;
source diff-check only. Implement exact plan, report missing evidence or cap
expansion instead of substituting a fake model/cache or broader framework.
Main reviews complete source, then strict CPU-only checks, bounded local Astra
oracle/lifetime review and necessary affected checks, explicit commit on2.0 only.

## Bounded review corrections

Verified Astra/xhigh review identifies correlated nearest/clipping containment,
missing automatic receipt availability gate, and warm same-world VM cache handoff
coverage. Retain production owners. Main adds an independent plane/convex-edge
half-space assertion over the selected integral face index, then leaves the
nearest query warm after the far refusal. Pass the actual warm point through the
existing fixture functions; query SSQC→CSQC→SSQC without reloading and again SSQC
after client clear, requiring unchanged result/cache count. Identical borrowed
world geometry is an explicit precondition; different-world cache ownership is
not certified. Private runner requires four inline witness passes/no unavailable
and completion marker. Expected correction <=55 C lines (overall<=365), no new
adapter/generator/production owner. Only affected final fixture needs rerunning;
old receipt/source retained before correction. Prepared attr5 arithmetic stays
bounded; untouched dedicated metadata safety remains unqualified.
