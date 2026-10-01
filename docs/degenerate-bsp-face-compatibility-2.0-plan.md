# Native two-edge BSP face compatibility

2026-10-01. Actual mj4m1 loading stops at Mod_LoadFaces: bad face, current
source55eaa33b. Main parsed the read-only BSP2 face lump:427881 faces, ten have
exactly two edges; plane/texinfo/surfedge ranges remain valid. First offending
face179825 has plane39763, firstedge860731, numedges2, texinfo41032. GDB locates
the product rejection at gl_model.c:2535–2540, not a texture or missing-asset error.

Native vkQuake gl_model.c Mod_LoadFaces accepts these faces; its triangulation
and current r_world.c R_NumTriangleIndicesForSurf return3*(numedges-2), zero for two edges.
Keep native loading/mesh ownership and stable BSP surface identities. The strict
migrated lower-bound guard rejects native-compatible content. No new mod-specific
case, surface compaction/remapping or replacement model loader is justified.

Smallest adapter: change only that generic face lower bound from3 to2. Retain
all plane/texinfo/surfedge bounds and rejection of zero/one/negative edges, which
would underflow native triangle counts. Preserve the zero-triangle native face;
do not invent visible geometry or mutate the asset. One changed expression,
approximately2 added/deleted lines, reopen before10. This is a required final
large-map compatibility repair attached to existing group6, not new feature scope.

Luna xhigh owns only Quake/gl_model.c, exact substitution/diff-check, no builds,
commits, branches/main/assets/SSH/children. Main reviews it and the affected
native polygon/count arithmetic, commits, rebuilds and retries actual mj4m1
load/render/leave without heapsize. Run relevant malformed range/low-edge cases
in final qualification; do not weaken unrelated loader validation. Complete
artifact revisions must include this change before final acceptance. User
performance timing remains excluded.

## Second native consumer exposed by the load retry

The92bc7775 retry passed model face admission, then stopped at the existing
GL_BrushRegenerationSourceValid lower bound in r_brush.c:1657. Main read that
helper and vertex fill/allocation: the same two edges have valid native sources;
bounded polygon_header + numedges*stride allocation and the per-edge fill loop
support two vertices. The native Vulkan index count remains zero. Ironwail only
warns for fewer than three edges and continues native loading, likewise showing
this is generic compatible content rather than a mod-specific exception.

Extend this same repair to that one lower-bound expression in Quake/r_brush.c,
3 to2, keeping every pointer/edge/vertex/range and allocation guard. The separate
ShowTris outline guard at681 remains unchanged: fewer than three vertices
need not generate a debug polygon. Main owns the exact second substitution;
combined source repair4 changed lines, within the original reopen-before10 bound.
Commit before the next affected load/render retry; no broader brush rewrite.

The next retry reached initial GPU vertex upload and exposed the same stricter
bound in GL_ValidateBModelVertexSources's aggregate-count pass (r_brush.c:2706).
Main read both count/span passes and the upload loops: two vertices are counted,
their assigned spans stay bounded, and the unchanged native triangulation emits
zero indices. This is the same admission policy at a third existing consumer,
not a new incompatibility or reason to replace regeneration. Extend only that
count lower bound to2, retain all aggregate overflow/submodel/destination/polygon
checks. Combined three substitutions6 changed lines, still below10. Main owns
this final exact substitution; search the complete loader/regeneration family
for equivalent local-count guards before another actual load retry.
