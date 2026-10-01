# Avatar muzzle-light adapter

2026-09-30. Before-code design; baseline `c7bd1932`. AV-002 source gap,
not a replacement lighting or avatar implementation. Software qualification is
deferred until the complete migration implementation is finished.

## Evidence and intended behavior

- [verified: direct source reads] Pinned primary
  `51b452c018273647dcf94f4628a370267ff8fa91`, `r_alias.c`
  `R_VRIKStoreMuzzleTransform` transforms the measured Ranger Gun tip into
  world space after a compatible draw. `cl_main.c:CL_RelinkEntities` uses this
  previous-render result for the existing muzzle dlight while younger than
  0.25 seconds. Otherwise it uses native body-origin/forward placement.
- [verified: whole-Quake symbol search and caller reads] Current `r_vrik.c`
  produces `muzzle_valid/origin/forward` and positions flare joints from it.
  `r_vrik_render.c` discards those output fields. Current client relink uses
  only native dlight placement. The geometry effect is already implemented;
  the missing consumer is its transient light origin.
- [verified: task/caller reads] CPU dlight marking occurs before efrag
  collection. Palette preparation depends on efrag collection and the frame
  slot's fence/buffer swap. Moving preparation before marking is not a local
  change. `SCR_UpdateScreen` joins `draw_done_task`, which depends on palette
  preparation, before returning; only end-render submission remains pending.
  The next client relink precedes the next palette-preparation task.
- [verified: attachment and raster reads] Alternate rigid equipment uses
  entity transform times `attached_prop_to_canonical` times bone-local mesh.
  Target-body display scale must not be applied to this prop. Custom native
  equipment has no canonical Gun socket and must use native light fallback.
- [unknown] Executable task safety and visible light alignment on real assets.
  No build, fixture, benchmark or game has been run for this checkpoint.

Goal: reuse the last completely prepared compatible tracked Gun presentation
for native muzzle-light placement, preserving the inherited bounded latency.
Native light keys, randomness, radius, color, lifetime, competing effect
precedence and interpolation flags remain owned by `CL_RelinkEntities`.
Desktop non-tracked players and incompatible/expired presentations retain
native behavior. This is cosmetic, not a projectile, aim or netcode change.

## Smallest adapter and open design decision

Lean: add optional world-space muzzle metadata to the existing frame-owned
prepared palette record, derived during candidate preparation and published
only with that complete palette. Expose a read-only main-thread getter for
client relink. Retain the source 0.25-second age bound; reject negative or
nonfinite age, source-model/avatar/generation mismatch, retired/inactive pose,
teleport/forced relink and reset state. Reset admission invalidates publication.
Do not borrow a geometry/palette allocation from this getter or load models.

Canonical Ranger uses the already solved muzzle point transformed with the
native alias matrix. An attached canonical Gun uses the selected prop's
bone-local measured tip (local +Y, 20 units) through its existing attachment
and native entity matrix. Axe and native custom equipment supply no socket.
No second solve, entity-side cache, allocation, lock or worker task is needed.

Alternative: store entity-side draw outputs as primary did. Rejected provisionally:
parallel draws would mutate client-owned state and duplicate frame publication.
Alternative: reposition existing dlights after current palette preparation.
Rejected provisionally: CPU marking has already consumed their origins; correct
implementation would require wider graph changes and effect provenance.
Alternative: infer the socket from controller aim. Rejected: it does not describe
the attached visible gun after avatar presentation transforms.

Requested local Astra review must verify the ownership/order claims and challenge
whether this adapter is necessary. Review the previous-frame choice, identity
gates and exact canonical/prop transform boundary; merge redundant checks and
avoid inventing a second admission protocol. Runtime effective reviewer settings
are not exposed by the orchestration tool, so returned advice is source review,
not certified execution or model-provenance evidence.

## Scope, integration and final qualification

Expected production write set: `Quake/r_vrik_render.c`,
`Quake/r_vrik_render.h`, `Quake/cl_main.c`; reuse the native alias matrix API.
Target under 150 added production lines, no task-graph or wire changes. Reopen
the plan before exceeding that boundary or introducing a new owner. Commit
this plan/review disposition before delegating the coding slice. Main reviews
the complete patch and source call chain; only scoped diff hygiene runs now.

After all implementation, qualify actual relink and render publication with two
independent tracked players, both hands, canonical and attached Gun versus Axe,
custom native gear, scaled/rotated entities, expired/nonfinite/negative time,
generation/identity change, inactive tracking, teleport, map reset, first frame,
allocation/admission failure, render tasks on/off and CPU/GPU lightmaps. Check
competing bright/dim/rocket effects keep native precedence and desktop fallback
stays native. No direct-helper fixture alone proves this integration. Device
alignment/performance trials and Windows builds remain deferred to the user.

## Source-review disposition before coding

Requested local Astra xhigh returned a bounded static review. Main checked its
load-bearing root/identity/reset claims against `R_DrawEntitiesOnList`,
`R_GetEntityLerpedTransform`, alternate candidate publication, `CL_FreeState`
and `Mod_ClearAll`. No effective-model metadata or execution evidence was
available. The review changed the contract before delegation:

| Recommendation | Disposition |
| --- | --- |
| Previous completed palette is the narrow timing boundary. | Adopt. No graph change, lock, GPU wait or second solve. Publication is preparation-driven and includes players not drawn; unlike primary it is not a draw-result cache. Failed/empty preparation yields native fallback. |
| Match first ordinary color-pass local-player root. | Adapt. Copy the entity, apply native local-player pitch multiplication to that copy before the existing lerped-transform helper, and use the native alias matrix. Preserve original pointer as identity. Do not multiply the interpolated output or mutate the entity. |
| Fix all native multipass/debug pitch behavior here. | Defer. Native MBOIT/debug passes may repeat the inherited entity pitch mutation; changing those working native owners is outside this consumer adapter. This adapter promises first ordinary color-pass root, not exact agreement with every debug pass. No user choice blocks restoring the ordinary light consumer. |
| Explicit source identity and reset retirement. | Adopt. Snapshot original model (distinct from alternate model), avatar ID, generation and relevant tracking/dominant-hand flags. Invalidate publication on admission reset and client free. Reject current stale/nonfinite/negative pose age, identity change and forced/teleported/backwards-time discontinuity; invalidate that record until fresh preparation. No pose-sequence equality requirement. |
| Select the actual tracked Gun. | Adopt. Gate attached metadata at existing Gun/Axe selection and require tracked solve muzzle validity; use prop alias matrix without target-body affine. Clear metadata at both candidate starts, including failed alternate fallback. |
| Cache forward as well. | Reject as unnecessary. Native dlight consumes only origin; publish finite origin/identity/time and copy it out. Getter may invalidate stale CPU metadata on the main thread after prior draw completion, never during render tasks. |

The three-file production boundary remains; an explicit publication-invalidation
helper may be called at client free as well as admission reset. No model loads,
wire fields or retained palette-memory accesses enter the light getter. Relevant
final cases additionally include head-only tracking, valid new pose sequences,
dominant-hand changes, backwards time and local-player first color-pass pitch.

## Source integration

Implemented in `c025acbc` after plan/disposition commit `76b63a8f`: 142 added
and 7 removed production lines across the three planned files. The bounded
local coding delegate implemented the slice; main reviewed the full diff and
native caller/transform/reset boundaries. Final requested local Astra source
review found no remaining P1/P2 blocker against the revised module contract.
Effective reviewer model metadata is unavailable; this is source advice, not
certified model or runtime evidence and not the full goal's final signoff.

The complete frame record owns origin, original-model/avatar/generation/flags
identity and timestamp; no forward cache, second solve, entity-side state or
additional task was added. Selected tracked canonical Gun attachment uses the
existing prop transform. Native custom gear, Axe, inactive/expired/changed
presentations and pending model reload fall back to native placement. Admission
reset and client free invalidate publication. Player discontinuity retirement
occurs even without muzzleflash; lookup work is restricted to relevant player
discontinuities and the existing flash consumer, not every map entity each frame.

Main scoped `git diff --check` passes. No build, compiler, fixture, benchmark,
game or headset test was run. Preparation-driven eligibility, first ordinary
color-pass root and unchanged native repeated multipass/debug pitch behavior
remain explicit limits. The linked consolidated qualification adds actual
publication/relink cases; helper-only evidence cannot close this consumer.
