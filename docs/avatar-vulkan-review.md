# Avatar renderer integration: Astra senior review

Date: 2026-09-26. Branch: `2.0`. The inherited `master` implementation is
the behavioral reference; vkQuake remains the renderer and model-loader base.
The identity protocol, local custom-package registry, and CPU retargeter are
present, but selected bodies are not yet drawn.

## Decision

Extend the existing frame-owned VRIK palette record into one immutable render
view keyed by the original `cl.entities` player, rather than substituting an
authoritative entity or adding another renderer. Resolve the selected model,
skin/geometry, canonical animation source, tracked pose if present, target
palette, presentation transform, conservative bounds, and optional equipment
parts before dependent render tasks. Raster, overlays, culling, and ray-query
BLAS/TLAS must consume the same resolved view. Keep persistent BLAS identity
on the original entity; rebuild if its resolved geometry changes.

Alternate desktop bodies also begin with **canonical Ranger animation** and
retarget to the selected rig. Tracking modifies that canonical source before
retargeting. The target monster's native AI animation is not the fallback.
Apply presentation normalization once and consistently to visible geometry,
bounds, and shadow geometry. A palette alone does not hide authored native
weapon triangles or attach the player's actual equipment; those require
explicit draw-part/index and attachment handling.

Use donor MD5 parsing, mesh upload, palette allocation, task graph, and GPU
retirement. Add a narrow optional-avatar loading entry point with nonfatal
failure; `Mod_ForName(path, false)` can still call `Sys_Error` on malformed
native MD5. Admit fixed built-in paths or locally digest-resolved packages at
a serialized stage **before** task dispatch. Register privately loaded
cosmetic meshes in existing buffer deletion/reload ownership. Commit fallback
to a coherent original/Ranger render view before any consumer sees it.

## Disposition

| Review finding | Decision | Implication |
| --- | --- | --- |
| Reuse the frame palette owner | Adopt | No parallel pose cache or CPU skinning renderer. |
| One model/palette is the whole avatar | Adapt | Resolve presentation and equipment parts in the same view. |
| Desktop uses the target's native animation | Reject | Retarget canonical Ranger animation on desktop too. |
| Replace entity pointers or only alias draw geometry | Reject | Shadow, lighting, textures, culling and overlays identify the original entity. |
| Nonfatal optional model admission | Adopt | Reuse MD5 parser with a narrow recoverable load path. |

The smallest vertical proof is one scene with two tracked peers and one
desktop peer selecting the same equipped alternate body. They must keep
independent poses and canonical animation, with matching raster/shadow
silhouettes and correct frustum-edge behavior. Switch a choice while frames
are in flight; check missing/malformed assets and loss of tracking, with task
recording both enabled and disabled. A matching custom package and a digest
mismatch are required before claiming custom-avatar completion. No user
headset test is required for this implementation gate.

## Verified references

`master:Quake/r_alias.c` around `5507` solves canonical Ranger animation
before retargeting; around `5570` prepares attached props; around `5581`
applies presentation to skinned vertices. Branch `2.0`:
`Quake/r_vrik_render.c` owns frame-slot palettes;
`Quake/r_alias.c` uses exact model/geometry/palette matches;
`Quake/gl_rmain.c` schedules palette preparation as a task;
`Quake/gl_mesh.c` and `Quake/r_brush.c` separately inspect original
`cl.entities` models for ray shadows; `Quake/gl_model.c` has a recoverable
MD5 parser below a fatal native-model call; and `GLMesh_DeleteAllMeshBuffers`
currently traverses precached models. Alpha sorting must depend on resolved
bounds if it reads them. The pure `Quake/r_avatar.c` bridge currently supplies
retargeted matrices, not complete presentation or equipment rendering.
