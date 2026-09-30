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
