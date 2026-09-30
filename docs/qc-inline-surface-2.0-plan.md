# QC inline brush-surface bounds

Source comparison found a concrete boundary error in three native surface-query
wrappers. Inline brush models share the world surface table and select their
range with `firstmodelsurface` / `nummodelsurfaces` (native `gl_model.c` model
setup). Destination `PF_getsurfacepoint` validates the requested vertex against
`surfaces[surfidx]` but then reads `surfaces[firstmodelsurface + surfidx]`.
`PF_getsurfacetriangle` similarly checks the wrong face's edge count. This can
reject a valid inline-face vertex or admit an invalid one. Triangle counts also
need the primary guard against faces with fewer than three edges.

Primary `Quake/pr_cmds.c` resolves `PF_GetBrushSurface` to the offset face first;
its point/triangle wrappers check that same face and return zero on invalid
requests. Native `PF_getsurfacepointattribute` already checks the offset face.

Keep native model retrieval, shared surface storage, vertex lookup, triangle-fan
representation, return locations and numeric/VM registry. Change the two bounds
checks to use `firstmodelsurface + surfidx`, matching the face actually accessed;
copy the primary three-edge guard into both triangle queries. The existing
short-circuit model/loaded/range checks must precede the offset face access.
No renderer/model rewrite or new surface-query service/helper is needed for
these demonstrated incompatibilities. Other query semantics remain subject to
the broader interface audit; this is not complete surface-API parity.

Write only `Quake/pr_ext.c` on `2.0`; this plan precedes code. Main source review
must trace the checked/accessed face and native model range setup. Final local
Astra and deferred Linux/ARM qualification must include real inline brush
queries for differing world/inline face edge counts, valid/invalid vertices,
degenerate faces, triangle boundaries, unchanged world-model results and zero
returns. Builds/tests/compiler/runtime probes remain deferred until the full
implementation is finished.
