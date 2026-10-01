# Shared alias-player root transform

2026-09-30. Before-code plan; baseline `46fc5710`. Reopens the bounded
muzzle-light review's deferred native local-player root limitation to satisfy
AV-002/007's existing same-pose raster/equipment/shadow requirement. Final
Linux/ARM qualification follows complete implementation; no early execution.

## Verified evidence and behavior

- [verified: current and pinned donor source] `gl_rmain.c:R_DrawEntitiesOnList`
  multiplies the local viewentity's `angles[0]` by 0.3 before model dispatch.
  `R_ShowTris` repeats the mutation. The native MBOIT alpha path invokes the
  ordinary entity draw path again. Non-alias behavior is outside this fix.
- [verified: current helper and all caller search] `r_alias.c`
  `R_GetEntityLerpedTransform` is read-only, used by alias raster/overlays/debug,
  prepared bounds, tracked body/attached-prop TLAS and muzzle preparation.
  Its callers are renderer code. Interpolated rotation uses message/previous
  angles; the other branches copy `e->angles`. The old pitch mutation affects
  only those copied-angle branches, not interpolated message angles.
- [verified: task graph] Tracked palette preparation precedes raster and TLAS.
  Those consumers have no mutual dependency. Mutating a shared player angle in
  a draw worker can race a shadow worker and differs from pre-draw muzzle
  preparation. Palette sharing alone does not fix that root mismatch.
- [verified: previous source repair] `c025acbc` computes first-color-pass muzzle
  placement using a copied entity with pitch damped before calling the helper.
  That workaround duplicates the same presentation policy at preparation.
- [unknown] Rendered agreement, runtime scheduling and executable behavior.
  Static evidence demonstrates the shared write and ordering, not a measured
  visual or performance result.

Preserve native first ordinary alias-player color-pass presentation for every
interpolation setting. All alias-player consumers must read the same unmutated
entity and apply that presentation policy once. Remote players, viewmodels,
ordinary aliases, animation/palette/skin behavior and native desktop graphics
remain unchanged except removal of repeated/racy local-player pitch damping.
Do not change the existing native non-tracked TLAS sign convention in this slice.

## Adapter versus new render owner

Lean: make the existing `R_GetEntityLerpedTransform` supply local alias-player
pitch damping only when its result used copied entity angles. Keep message-angle
interpolation exactly native. Guard local-player identification with valid
`cl.entities` and viewentity bounds, model presence and alias type. Remove
alias-player shared-angle writes from ordinary/debug draw dispatch; retain
their existing behavior for non-alias local models. Muzzle preparation calls
the shared helper with the original entity and removes its private copy/damping.

This reuses the existing renderer helper and all existing consumers without
additional state, a second root cache, frame records, task edges, model loads,
allocation or locks. New cached root matrices would duplicate a deterministic
helper and introduce invalidation policy. Serializing raster against TLAS
would retain the mutation and constrain native parallelism. Applying damping
to every final interpolated pitch would change ordinary native presentation.

Expected production write set: `Quake/r_alias.c`, `Quake/gl_rmain.c`,
`Quake/r_vrik_render.c`. Target fewer than 50 added lines and deletion of the
private muzzle entity-copy workaround. Existing helper signature stays stable.
Request local Astra source/design review and record its disposition before
delegating coding. Main verifies load-bearing recommendations, full diff and
scope. Reopen the design if another mutable owner, cache or task edge is needed.

## Open decisions for review

1. Does this narrow helper policy preserve the first ordinary color-pass result
   for copied versus interpolated angles, including `r_lerpturn` and EF_ROTATE?
   Lean yes, provided we track which branch supplied the output angles.
2. Is gating dispatch mutation to non-alias sufficient, or does a read of the
   current call graph reveal another alias mutation? Lean sufficient from the
   whole-Quake search; verify debug/alpha and attached/shadow consumers.
3. Is extending the immutable frame record necessary? Lean no: inputs are
   read-only through these tasks once the native draw write is removed.

Solo project, bounded renderer policy repair. Do not re-review the full avatar,
OpenXR/foveation, protocol, general native graphics or excluded VR demo design.
Effective review-model metadata is not exposed by the orchestrator; label the
returned review as source advice, not certified model/runtime evidence.

## Final software qualification

After all implementation, use actual alias draw/preparation/TLAS owners with
local tracked Ranger and an equipped alternate, remote and ordinary aliases,
both desktop and stereo, tasks on/off, opaque and MBOIT alpha, repeated debug
passes, overlays, and CPU/GPU lightmaps. Check the player entity angles never
change from these alias consumers. Cover movement/turn interpolation on/off,
tagentity, EF_ROTATE, nonzero pitch/roll, scale and invalid/missing local entity.
Require same root for body/equipment/muzzle and tracked shadow, with native
first-color-pass values in each branch. Preserve non-alias and viewmodel native
behavior. A helper-only fixture does not prove shared-owner integration.
Live headset/performance trials and Windows builds remain deferred.

## Before-code source-review disposition

Requested local Astra xhigh returned a source-backed review. Main checked its
load-bearing interpolation/caller claims and the read-only alias matrix body.
Effective reviewer settings were not available for certification. No runtime
or full-goal signoff is claimed.

| Recommendation | Disposition |
| --- | --- |
| Damp copied-angle branches only. | Adopt. One local branch-result boolean; no duplicate interpolation predicate and no damping of message-angle interpolation. |
| Bounded local-viewentity alias identification. | Adopt. No extra tracking, avatar-name or scoreboard-role policy. |
| Restrict ordinary/debug entity mutations to non-alias models. | Adopt. This removes the alias raster/TLAS race and repeated alpha/debug damping while preserving native non-alias dispatch behavior. |
| Delete private muzzle interpolation copy/damping. | Adopt with API adaptation. Pass the original const entity to the shared transform helper. Native R_AliasModelMatrix's parameter is non-const but its implementation only reads the entity; use a narrowly documented cast at that call, preserving the existing API rather than spreading signature edits. |
| Add a cached root or serialize raster against TLAS. | Reject. Existing inputs become read-only once the demonstrated alias draw write is removed; the deterministic native helper already supplies the root. |
| Change the native non-tracked TLAS pitch convention. | Reject as adjacent scope. Tracked body/prop roots are shared; ordinary native shadow convention remains unchanged. |
| Import primary's mutable interpolation history. | Reject. Reuse vkQuake's read-only interpolation helper and retain source attribution; primary establishes the legacy pitch behavior only. |

No human choice is needed in this bounded repair. Three production files,
unchanged signatures and fewer than 50 added lines remain the coding contract.
