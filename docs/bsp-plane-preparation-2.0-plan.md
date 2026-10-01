# Retained BSP plane and face preparation at the native loader

2026-09-30. Before-code brief for PERF-F002 and existing asset/map correctness.
Baseline `1cb5a2eb`; production ownership remains native `gl_model.c`.
No plane/face implementation change yet. Requested-Astra source-design advice
accepted the narrow slice; the disposition below precedes coding.

## Verified current and retained reference evidence

- Current `Mod_LoadPlanes` allocates twice the declared plane count and writes
  only the first count. The retained XR3080841333fa94000df7e1fb9e549c7158685dd6
  `gl_model.c:3540–3573` allocates exactly count and initializes all plane fields
  and padding. It does not contain a new compact plane-reference API. Native
  mark-surface indices and parallel geometry jobs already exist on2.0.
- Current `Mod_LoadFaces` uses nonzero-filled native surface allocation, then
  ORs style bits into `out->styles_bitmap` without initializing it first.
  `r_brush.c` consumes it for visibility/lightmap dependency and modified masks.
  Retained XR's older hunk allocation zero-filled that state implicitly. Reuse
  the native allocation and initialize only this missing accumulator explicitly.
- Current face loading forms plane/texinfo pointers and accesses unlit polygon
  geometry before any local face-index/span check. The retained XR loader
  checks minimum edge count, bounded surfedge span, plane and texinfo indices
  before those consumers (`gl_model.c:2764–2770`). The arithmetic uses remaining
  span rather than an overflowing firstedge+numedges sum.
- All three native node-format readers form `mod->planes+p` without a local
  bounds check; retained XR has owner-thread plane checks in each reader. Native
  clipnode readers already check their plane indices; no replacement required.
- Declared native plane/hull pointers retain model ownership. Whole-family
  producer/consumer verification is required before removing the spare half;
  matching declarations or a search result alone is insufficient proof.

## Minimal adapter versus representation rewrite

Lean: retain every native model/plane/surface/hull type and owner. Once source
review verifies no valid consumer needs the unused allocation half, remove only
the factor2 while keeping native zero-filled `Mem_Alloc`. That avoids a new
no-fill/padding policy. Restore retained face/node guards at owner-thread decode
boundaries before pointer creation, and zero styles_bitmap once before either
face-format branch. Keep native unaligned readers, existing signed BSP29 face
interpretation, allocation/free ownership, indexed tasks and join ordering.

No compact-index conversion, parallel plane array, alternate loader, new task,
metadata cache or allocator is justified. The reference's older Hunk/SDL worker
implementation is not reusable ownership on the vkQuake base. Do not silently
expand into full malformed-BSP/edge/vertex/light-data validation or change native
extent arithmetic/error policy. If a demonstrated wider boundary is required,
report it and reopen this plan rather than patch adjacent loader systems.

Expected production write set: `Quake/gl_model.c` only, fewer than35 added
guard/initialization lines and one allocation-factor change. Native compact
marksurfaces/geometry workers remain, without another structure. Source advice
must settle valid plane domains and spare-half usage before delegated coding.
Main verifies load-bearing evidence, records disposition and commits the plan;
then reviews the exact patch and requests final bounded source review.

## Required source review

Verify all native model-plane producers and collision/render/particle consumers,
including hull0 generated indices, clipnode validation and inline model sharing.
Identify any actual count..2*count use or mutation requiring the spare half.
Challenge whether the narrow face guards suffice for this exact retained plane
boundary without claiming complete malformed-BSP safety. Confirm current style
accumulator initialization and downstream masks. Effective reviewer settings
remain uncertified; runtime and rendered behavior are unknown.

## Final software qualification

After all implementation, actual native loading/visibility/collision/particles/
lighting must retain ordinary BSP29,2PSB/BSP2, Valve and Quake64 paths, inline
brushes and serial/worker modes. Exercise zero-style and animated-style surfaces
through real dependency/dirty-lightmap consumers, reload and CPU/GPU lighting.
Use disposable malformed face spans/plane/texinfo and node-plane inputs to
confirm owner-thread refusal before invalid pointer/worker submission.
Named large maps remain final software load/exit checks; no measured heap/RSS/
frame-time or full parser-fuzzing claim follows from this bounded change.
Windows and live headset/performance trials remain deferred. No tests, builds,
compiler probes, games or benchmarks before complete implementation.

## Before-code source-review disposition

Requested local Astra xhigh source advice verified the native whole plane/hull/
render/particle family. Main checked the missing face accumulator and the
separate padded SIMD plane allocation. No valid consumer of the second half of
model plane storage was found. No builds or execution-based verification ran.

| Recommendation | Disposition |
| --- | --- |
| Allocate declared plane count only. | Adopt. Face/node pointers and validated clipnode indices stay in the declared domain; hull0 derives indices from node pointers. Inline models share the owner's storage; separately padded SIMD and box-hull arrays remain untouched. Keep zero-filled native Mem_Alloc. |
| Initialize the face lightstyle accumulator before either decoder. | Adopt P2 correctness fix. Preserve styles normalization and zero-to-one fallback. Dependency and dirty-lightmap masks consume the value; sorting uses styles[] directly, so no sorting-corruption claim. |
| Restore face and all three node plane guards before pointer construction. | Adopt P2 boundary fixes from retained XR. Use subtraction-safe surfedge span, minimum edge count, and plane/texinfo bounds. Keep signed BSP29 decoding and native joined geometry tasks. |
| Expand into a new plane representation, no-fill allocator, or full BSP validation. | Reject unsupported adjacent scope. No consumer incompatibility justifies a new representation or task. These guards do not prove full malformed-BSP safety. |

Production write set remains only Quake/gl_model.c, fewer than35 added lines.
Final exact-patch source review and end-of-implementation software qualification
remain required. Effective reviewer settings and runtime behavior are unverified.

## Source integration receipt

Production commit `3c440916` changes only gl_model.c:16 additions and1 removal.
It restores the retained face guard and three node plane guards, initializes
the face lightstyle accumulator before either decoder, and removes the unused
plane allocation factor while preserving native zero-filled ownership.

Main reviewed the entire patch. Requested-Astra final bounded source advice
found no P1/P2 issues; subtraction short-circuit order, signed decoding, task
joins and inline ownership remain. Scoped diff --check passed. No builds/tests/
compiler/runtime or measured performance qualification ran. The final Linux/ARM
plan includes actual loading/render/collision/lightmap and refusal boundaries.
