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

Source review addition before the remaining edits: native `Mod_LoadSubmodels`
reads the first-face/count values directly, and `Mod_SetupSubmodels` copies them
onto shared model storage without an absolute surface-table bounds check.
Primary `PF_GetBrushSurface` checks the absolute face against `numsurfaces`.
For these three wrappers, also require nonnegative `firstmodelsurface`, positive
`nummodelsurfaces`, a base below `numsurfaces`, and an index below the remaining
table length before adding base and index. The subtraction form prevents index
addition overflow. Keep these local guards rather than introducing a new model
loader policy or rewriting neighboring surface services.

Write only `Quake/pr_ext.c` on `2.0`; this plan precedes code. Main source review
must trace the checked/accessed face and native model range setup. Final local
Astra and deferred Linux/ARM qualification must include real inline brush
queries for differing world/inline face edge counts, valid/invalid vertices,
degenerate faces, triangle boundaries, unchanged world-model results and zero
returns. Builds/tests/compiler/runtime probes remain deferred until the full
implementation is finished.

## Implementation source checkpoint

The three native wrappers now validate the inline base and remaining table range
before accessing the requested face. Point and triangle bounds use that same
face; both triangle queries reject fewer than three edges before subtraction.
Main source review traced the signed model range fields, shared surface-table
copy, short-circuit guard order and existing zero-return paths. For valid loaded
world/inline models, native vertex lookup and fan indices remain unchanged.
Malformed vertex/edge storage is still the loader's existing responsibility;
this slice does not establish a full malformed-BSP or surface-interface contract.

No builds, tests or compiler/runtime probes were run. Final local Astra review
and consolidated Linux/ARM software qualification remain pending.
