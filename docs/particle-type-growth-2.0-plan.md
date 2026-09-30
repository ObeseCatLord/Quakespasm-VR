# Scripted particle type growth: preserve the native renderer

2026-09-30. Verified source brief before implementation. MOD-009/010 keep
vkQuake's particle visuals, Vulkan batches and native threaded task graph. Fix
only a demonstrated inherited type-array lifetime boundary, if the bounded
requested-local-Astra source assessment confirms the change. No current runtime
failure or numeric speedup is claimed; builds/tests remain at end of the goal.

## Verified facts and unknowns

- Native `r_part_fte.c:937–993` P_GetParticleType searches the current array,
  reallocates for every new type, then repairs run-list and nexttorun links by
  subtracting old-array pointers after the allocation has been replaced.
  It does not repair shared `slooks` or initialize the new type's `slooks`.
- Current primary `51b452c0` `r_part_fte.c:932–1065` already captures link
  indices before realloc and restores them afterward, and immediately rebuilds
  shared looks. Reuse its lifetime-preservation behavior; retain native Mem_*,
  task ownership and batches rather than port OpenGL rendering.
- Native setup `:6625–6670` rebuilds looks, clears r_plooksdirty, then calls
  CL_RegisterParticles and PScript_RecalculateSkyTris. Those invoke namespace
  lookup; FindParticleType can load another set and grow the array. Worldspawn
  texture effects are looked up at`:3464`, model effects through registration.
- Native layout and indexed emission dereference slooks at`:6834/7007/7045/7382`
  after setup. A new allocation during registration can therefore invalidate the
  just-built look pointers before the same-frame consumers; no malformed input
  is needed for the source-level lifetime problem.
- Native frame owner schedules setup→indexed update→layout→indexed emit→draw
  once at `gl_rmain.c:2911–2932`; the serial branch mirrors it at`:2984–2990`.
  CL_RunParticles and temp entities remain client/host-owned. Stereo draw uses
  native multiview pipeline adapters; no per-eye simulation loop is needed.
- Type indices, association/inwater/emit/clip indices and external model/precache
  indices must remain stable. Removal frees sounds/ramps, then the type array
  (`r_part_fte.c:3339–3350`). All additional reserve state must reset there.
- Unknown: every caller's live local pointer/input-name lifetime and any
  type growth overlapping indexed consumers. Verify these immediate boundaries
  before selecting a fix; do not claim full particles or mod compatibility.

## Options and current lean

The smallest correctness adapter can copy primary pre-realloc link capture and
rebind shared looks at the same boundary, then rebuild sharing after all setup
lookups. That retains native realloc but adds temporary repair indices per grow.
Copying primary's all-pairs sharing rebuild after every new type repeats work
that native setup already owns; avoid it unless an actual caller requires it.

An alternative uses a geometrically reserved contiguous array through native
Mem_*: allocate/copy, repair run/next/look pointers while the old array is still
alive, then free old storage. This avoids subtraction after free and temporary
repair arrays, and reduces repeated cold type-array copies. It adds one private
capacity counter, not another particle registry, schema or renderer. Preserve
name/namespace strings when inputs might alias the old allocation. Establish
own looks for new types; sharing stays the setup owner's job. Check allocation
arithmetic and preserve native allocator failure policy.

Lean toward this bounded growth adapter plus ordering the existing setup
registration/sky/default lookups before final sharing rebuild. Compare against
the minimal primary-index port before adoption. Any new scheduling owner,
persistent side tables, registry rewrite or particle renderer is rejected.
Do not change lookup/alias/retint/namespace precedence, appearance, emission
timing, RNG, blend modes, UI, shaders or simulation. No mod-name checks.

## Scope and acceptance

Expected write set only `Quake/r_part_fte.c`, target at most100 net production
lines; reopen if exceeded or task ordering grows beyond the existing setup node.
Read-only reviewer verifies actual call paths, identifies the smallest safe
adapter and critiques overhead/deletion opportunities. Record disposition and
commit it before production edits. Effective reviewer settings are not exposed;
do not claim a formal skill/model pass from requested Astra/max alone.

Later consolidated Linux/ARM checks cover new namespaces/effectinfo and retints
with live linked types, same-frame registration/texture weather, map reset/reload,
task/serial modes and distinct stereo output without double advancement.
Native desktop classic/FTE visuals and draw batches remain the reference;
actual user headset/performance checks are deferred. No tests now.

## Adopted requested-Astra disposition and reopened boundary

Main spot-checked native GUI/particle dependencies, serial GUI ordering,
allocator nullability, setup order, lazy namespace lookup and retint run-list
insertion. The requested-Astra/max source assessment changed the decision:

| Finding/recommendation | Disposition |
| --- | --- |
| GUI CSQC_Hud can request a namespace while particle workers hold raw pointers | Adopt one existing dependency: draw_particles_task precedes draw_gui_task when present. Serial native ordering already draws GUI after V_RenderView. Keep GUI effects for the next particle pass; no new worker locks or owner. |
| Capture pointers before realloc and repair links afterward | Adopt primary index-capture behavior through one temporary native Mem allocation. Capture nullable run head/next links while live, guard count/size arithmetic, check allocations, restore from indices without post-free arithmetic/comparisons. Bind every type's slooks to its own looks until the existing shared-look rebuild. |
| Geometric reserve does not close the race or reset-to-defaults look gap | Reject reserve in this slice. No capacity counter/reset policy. Keep contiguous native array, indices and allocator ownership. No measured speedup is claimed. |
| Registration/sky lookups can add/reset types after look rebuild | Adopt defaults → clear dirty → registration → sky lookup → existing sharing loop. Preserve late dirty invalidation for another registration pass next frame; no fixed-point loader or per-append all-pairs rebuild. |
| Parser can overwrite com_token/va inputs during lazy load/retint recursion | Adopt exact-content query/name preservation around nested lookup/loading slow paths; use the existing owned cfg->name after P_LoadParticleSet creates it. Do not truncate or change namespace/alias precedence. |

The earlier one-file/setup-only boundary is reopened before implementation:
authorized production write set is now `Quake/r_part_fte.c` and the single task
edge in `Quake/gl_rmain.c`, still at most100 net lines. No renderer/shader or
protocol changes. Main's graph inspection shows draw_gui→draw_done and
draw_particles→draw_done; adding particles→GUI introduces no reverse edge.
No implementation may proceed by ignoring the GUI growth path.

The advisory also found primary/native retint copies PS_INRUNLIST while clearing
destination links, preventing insertion of a new clone if the source is live.
Separate adopted ownership disposition within this same existing helper: clear
only that copied membership bit when clone links/particle lists are cleared.
Preserve all appearance flags and other state; no second list or retint renderer.
This is an intentional native/reference bug fix, not a claim of exact prior
visual parity. Add live-source retint to final software acceptance.

Main retains source-only limits and broad MOD-009/010 qualification. Effective
reviewer settings are unavailable, so no formal skill/model pass is claimed.

## Final source recheck: complete reader ordering

The bounded final Astra recheck accepted the allocation/look/name/retint changes
but found two more existing readers not excluded by particles→GUI alone. Main
spot-checked both owners and the actual graph. Reopen the task-edge estimate
before integration; the same two-file write set and100-net-line cap remain.

| Finding | Disposition |
| --- | --- |
| R_StoreEfrags emits static-entity particles while setup can grow/reset types | Adopt store_efrags→update_particles_setup_task. R_MarkSurfaces returns the actual efrag task in both split/combined variants; native serial rendering already marks before particle setup. No opposite dependency exists. |
| GUI classic fallback can mutate the live list while show-tris counts/draws it | Adopt draw_view_model_task→draw_gui_task when the existing r_showtris task-dependency branch runs and GUI exists. This keeps ordinary GUI exclusion and diagnostic readers ordered, without an unconditional new barrier or lock. |

These are lifetime/threading repairs to existing vkQuake particles, not missing
mod particle types or a renderer port. Runtime/stereo/visual qualification stays
in the final software pass. Source recheck of these exact added edges precedes
the production commit; no new allocation or task owner is introduced.

Main's final reader inspection also moves RunParticleEffectState's type-pointer
formation after its existing index check. P_INVALID from failed precache lookup
must return the existing fallback without forming an out-of-array/null-base
pointer first. This preserves the guard and visible fallback behavior.

## Source integration checkpoint

The integrated two-file production patch is52 net lines:47 particle and5 graph.
It copies the reference pre-growth index repair, retaining native Mem ownership,
with overflow/allocation guards and no expired-pointer arithmetic. Own-look
binding, final registration/look ordering, exact nested query-name lifetime and
retint membership are repaired without changing particle types/materials/shaders.
The final bounded local Astra recheck found no source blocker in the adopted
reader-ordering edges or invalid-index correction. Main inspected the full patch
and scoped `git diff --check` passes. No builds/tests/probes were run.

MOD-009/010 broader content/visual/software coverage remains open. This fixes
demonstrated native lifetime/threading defects; it does not certify every mod
effect or claim an improved frame time. Final Linux/ARM qualification remains
after the full implementation pass.
