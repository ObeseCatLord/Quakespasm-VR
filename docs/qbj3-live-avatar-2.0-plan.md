# C12: inherited QBJ3 live-avatar admission

2026-10-01. Before-code plan; builds and executable checks remain deferred.

## Behavior and verified reference

Restore the inherited cosmetic QBJ3 live-player selection at the existing
main-thread avatar admission boundary. Explicit resolved avatars take precedence.
The native Ranger choice may select the installed `qbj3` avatar only for an
active tracked live player with viewer posing enabled. Missing, failed or
unresolved packages retain QBJ3's native player art, without substituting Ranger.
Desktop rendering of explicit avatars remains supported without local VR.

Verified reference: primary `Quake/r_alias.c` functions
`R_VRIKQBJ3PlayerFrame`, `R_VRIKQBJ3LivePlayer`, `R_VRIKQBJ3Avatar` and
`R_VRIKSubstitutePlayer` (5732–5895). The supported model is
`progs/player_qbj.mdl`, exactly 143 frames, current ordinal 0–142. Ordinals
41–102 are deaths. The implicit package requires its existing Ranger equipment
policy. A nonempty unresolved `avatar_custom_keys` descriptor must not be
mistaken for an explicit Ranger selection.

Current `R_VRIKRenderStageAvatar` and `R_VRIKRenderAlternateCandidate` admit
only `progs/player.mdl`; raster `R_AliasUsablePalette` and BLAS
`R_EntityBLASPalette` repeat that restriction. The existing staged models,
resolved rig caches, model-owned skin/geometry, frame palette upload, culling,
attachment and ray-query owners are otherwise reusable. No demonstrated need
exists for another renderer, CPU vertex skin cache, identity protocol or rig.

## Smallest adapter

Copy the strict QBJ3 frame/live predicates, with the existing game eligibility,
into the current render preparation owner. Expose one read-only original-model
eligibility helper to keep raster and BLAS admission consistent. Factor only
the existing selection decision enough to resolve explicit versus implicit
package at staging; retain all geometry, digest, skeleton and profile checks.
Require the canonical source to cover all 143 ordinals for this path.

At alternate candidate preparation recheck live/frame eligibility and require
fresh tracking for an implicit QBJ3 choice, so staging cannot publish that
choice after its tracking premise expires. Explicit choices still animate
ordinarily without tracking. Do not open death/corpse substitution yet: C13
will add its separate palette and identity behavior under its own plan.

## Ownership and expected scope

Production write set: `Quake/r_vrik_render.c`, `Quake/r_vrik_render.h`,
`Quake/r_alias.c`, `Quake/gl_mesh.c`. Approximately 80–140 added/changed lines;
reopen if this requires a new resource/state owner or mesh implementation.
Preserve the newly integrated sampled body-yaw publication and viewer toggle.
No asset files or other checkouts may change.

## End-of-implementation acceptance

Use installed supported QBJ3 assets and software-controlled poses: native choice
with active tracking selects the matching rig; explicit alternates win; explicit
alternates animate in desktop; viewer toggle/off or stale/inactive tracking
disables implicit substitution. Unsupported names/counts/ordinals, unresolved
custom descriptors and missing packages retain original art. Raster, culling,
BLAS/TLAS, skin/fullbright, root yaw and muzzle selection agree. Death/corpse and
optional QBJ3 detached equipment acceptance belong to C13/C14 and are not closed
by this slice. No early tests or runtime claims.
