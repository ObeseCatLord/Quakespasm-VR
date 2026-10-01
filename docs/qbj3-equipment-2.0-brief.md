# C14: inherited QBJ3 equipment — senior design brief

2026-10-01. Solo-maintainer bounded inherited exception. C12/C13 integrated,
including explicit death/corpse animation. No tests/builds/executable probes until
all implementation is finished. Main draft; local Astra verifies/critiques before
a before-code plan. Only 2.0 writable, all primary/assets read-only.

## Verified reference and current seams

| Fact | Evidence |
| --- | --- |
| Primary admits only exact authored equipment package | [verified: readonly primary Quake/r_alias.c:5262–5277] QBJ3 game, selected target equipment_policy ATTACH_HAND, installed custom key qbj3 with digest ef98e3b1df7cf03715dbd54963329c84f91abf490a19f9faafd7e694c693dc42. |
| Two independent anchors required | [verified: primary r_alias.c:5280–5374] roots QBJ3_Shotgun / QBJ3_BackWrench must parent to source HAND_R / SPINE2. Shotgun target hand follows accepted left dominance; wrench target SPINE2. Transport canonical bind points through rigid target sockets, both share native atlas. |
| Primary freezes optional prop geometry from bind points | [verified: primary r_alias.c:5325–5369] weighted bind-point sum, source presentation point mapping, then attachment affine; fully owned triangles only. No equipment animation solver. |
| Root ownership allows descendant/multiple weights | [verified: primary r_alias.c:4623–4638] summed descendant weight>=0.999999 per vertex; every triangle corner must qualify. Single-weight capture is insufficient unless actual data proves it. |
| Optional failure never hides selected body or substitutes Ranger gear/muzzle | [verified: primary r_alias.c:5294/5570–5577] clear derived prop state before optional preparation, return void; QBJ equipment branch does not enter ordinary Ranger attached-prop fallback. |
| Current model owns reusable CPU/GPU prop arrays | [verified: Quake/gl_model.c:87/102/5696/5815/6907–6911/7512] two prop slots, compact vertices/indices, source texture borrow, static one-joint views, renderer upload/free owner. Existing capture uses only exact Ranger leaf/single-weight mesh. |
| Current frame record represents one attached prop | [verified: r_vrik_render.c candidate fields/AttachProp around949; r_vrik_render.h:33; r_alias.c:222/1076/1198/1244/1302; r_brush.c:90–104] one geometry+affine+valid, consumed by raster/showtris/bounds/TLAS. Shader material path already supports native MD5 prop views. |
| Current generic attachment uses canonical Ranger policy | [verified: r_vrik_render.c949–1054] chooses nearest Gun/Axe, target hand socket, optional muzzle, AS-ready gating. This is wrong source/policy for QBJ3 monster avatars. |
| C13 gives one bounded frame owner, current selection and native death palette | [verified: f96ad216] candidate/prepared entries and each corpse own palette; no tracking/root/muzzle for supported QBJ death/corpse. C14 must preserve this owner. |
| Installed assets and all native structure counts/socket availability | [unknown] inspect read-only installed exact matching package during source work; qualify actual rendered/native ARM output at end. Do not alter assets or assume weights/leaf structure. |

## Worked minimal design

Reuse model-owned two prop slots and their static one-joint Vulkan upload/BLAS
owner. At model admission, only the exact digest/socket contract opts into QBJ
extraction. Extend the existing compact capture helper narrowly: sum fully owned
weighted bind points, convert into the supported root's bone-local space, retain
source UV/material, compute area-weighted normals once. Do not add per-frame CPU
skinning/streaming or a parallel mod prop cache. Missing optional QBJ source/gear
must not make an otherwise admitted custom body fail MD5 loading.

Stage the optional QBJ source before workers can touch model/texture tables;
selection borrows model-owned geometry/skeleton/bind identity. Revalidate source
digest/residency/rig/views when preparing. The source profile/context maps its
bind anchor into canonical Ranger coordinates; target body already has accepted
palette/rig/presentation. Copy primary rigid socket transport exactly, adapting
bone-local static views through the existing affine helper. No live tracking on
deaths/corpses; source/target animation authority unchanged. Missing source,
socket/view/material or AS readiness clears optional props and keeps valid body;
do not enter Ranger prop or derive a muzzle for QBJ3 equipment.

Extend candidate/prepared attached prop state to a bounded two-element record
array (geometry/affine/valid/bound), shared by raster, tris, conservative culling
and TLAS. Ordinary Ranger path publishes one record with unchanged behavior;
QBJ path publishes the two independent attachments only after optional complete
validation, preserving primary no-partial-pair behavior. Native model prop arrays
already hold two meshes; GLMesh static BLAS iteration reuses them. No new shader,
descriptor policy, entity, renderer, animation or gameplay layer.

Alternatives considered:
- Keep one record and drop wrench: rejected, loses required inherited behavior.
- Combined two-joint stream and per-entity palette: can retain one draw record,
  but replaces static prop identity/descriptor/upload policy and adds bone/shader
  work where two existing static views are reusable.
- Copy primary per-frame CPU transformed vertices: behavior reference valid,
  but unnecessary given existing model-owned rigid stream/socket/BLAS owners.
- New mod-general equipment registry: rejected; only inherited QBJ3 exception
  authorized. Exact digest/socket boundary avoids accidental package matching.

## Open decisions / review scope

1. Challenge whether two bounded records is the minimum complete adaptation,
   and whether any current native owner already represents both without change.
2. Validate optional-load failure/body admission boundary. Avoid enabling optional
   gear extraction in a way that rejects whole custom model or leaks partial views.
3. Verify canonical bind/socket/root-local conversion needed to reuse existing
   R_AvatarBuildAttachedPropTransform; identify duplication that can be deleted.
4. Rank lifetime/raster/BLAS/skin/glow/bounds risks; propose bounded write slices
   and estimates before coding, including no stale optional outputs on slot/reset.

Expected writes after approved plan: gl_model.c, model metadata header if needed,
r_vrik_render.c/h, r_alias.c and r_brush.c; gl_mesh only if existing generic prop
BLAS truly requires adaptation. Rough estimate350–550 changed lines, reopen on
material growth or another owner. Main owns integration, C20 temporarily owns
gl_screen and C07 phase1 owns cl_main/client/view files; no overlapping edits.

Review read-only source, no edits/tests/builds/probes/subagents. Verify first and
return <=1600 words prioritized disposition candidates with file/line evidence,
unknowns and genuinely human decisions. Do not re-audit unrelated avatar rigs,
networking, foveation, physics or excluded mod features. Reuse is the objective.
