# C14: two inherited QBJ3 attachments at native owners

2026-10-01. Before-code plan after local Astra xhigh review of
[the verified brief](qbj3-equipment-2.0-brief.md). Main independently verified
effective gpt-6-astra/xhigh from matching model/effort metadata; reviewer could
not verify its own routing. Static source/assets review only. No tests/builds
until every required implementation item is finished.

## Senior disposition and main spot-checks

| Recommendation | Disposition |
| --- | --- |
| Reuse model-owned two slots; fixed two frame attachment records | Adopted. Existing static identity views/BLAS suffice; a surface chain with one transform cannot represent hand and spine anchors. No new shader/upload/animation owner. |
| Optional QBJ capture must not share fatal Ranger admission policy | Adopted. Main checked root/capture/upload MD5ERROR paths and CustomAvatar_MarkFailed cleanup. Recoverable optional pair failure frees local prop arrays/views and continues valid body admission; do not free borrowed body textures. Native Vulkan Sys_Error convention remains, no general GPU-OOM recovery promise or human approval needed. |
| Existing live alternate also needs body fallback | Adopted. Main checked current generic failure returnsfalse before alternate publication. QBJ+ATTACH_HAND branch is selected regardless of equipment availability; optional failure publishes no gear/muzzle but retains valid body, never substitute Ranger attachment. |
| Use root-local bind positions and compose scaled source map after rigid socket | Adopted. Main read R_AvatarBuildAttachedPropTransform orthonormal requirements and primary rigid-basis/full-origin separation. Identity source_prop_pose yields socket; final affine is socket * source_presentation.forward * root_bind. Do not feed scaled source matrix into orthonormal helper. |
| Reuse baked bind points, do not recompute weights | Adopted. Main checked MD5_BakeInfluences sums all premultiplied original influences and bind capture retains baked xyz. Convert existing bind xyz through root inverse. Ownership uses original descendant biases>=0.999999, not truncated GPU influences or a second bias multiplication. |
| ShowTris currently omits props, while co-op overlay includes them | Corrected brief. Main checked actual functions. Extend both prop records in raster/overlay/ShowTris; all masks precede outlines. No stale single-prop field consumers remain. |
| Reuse source staging and existing shared prop BLAS | Adopted. Main checked GLMesh discovery visits both model slots. Extend only TLAS count/emission consistently, retain static geometry owner. Revalidate optional source/view identity; no worker loads or per-frame digest hashing. |

Exact inherited contract: QBJ3 game; target ATTACH_HAND; source custom key qbj3
with digest ef98e3b1df7cf03715dbd54963329c84f91abf490a19f9faafd7e694c693dc42.
Source roots QBJ3_Shotgun and QBJ3_BackWrench must directly parent source
HAND_R and SPINE2. Shotgun source anchor always HAND_R; accepted left dominance
selects target HAND_L only. Wrench remains target SPINE2. Source package's normal
profile/context is used without target humanoid/floor normalization.

## Phase1: optional model-owned extraction

Exclusive writes Quake/gl_model.c, optional minimal gl_model.h comment/metadata
if demonstrated. Prefer no header change. Existing slot GUN means shotgun and
AXE means wrench for the exact equipment source; source body remains complete.

Recognize exact custom descriptor key/digest at existing MD5 admission. Existing
Ranger capture/strict single-weight leaf policy remains unchanged. Narrowly
extend mask/capture inputs for QBJ descendants: fully owned triangles, compact
vertices/indices, root-local existing baked bind point, original UV/borrowed
source skin/glow and computed area-weighted normals. Reuse upload/free helpers
and bounded allocations. No new model cache/registry, imported asset or per-frame
CPU geometry stream. Root inverse uses existing finite rigid math.

Capture both local arrays, upload both views, publish complete pair only. Any
recoverable QBJ root/geometry/view/helper failure clears local views/CPU arrays,
counts/tails/budget state, disables subsequent optional capture and continues
ordinary valid body loading. No MarkFailed/MD5ERROR for optional equipment.
Native malformed-body validation and fatal Vulkan allocation policy stay native.
Snapshot all model-owned optional output in the existing success transfer;
existing model/free/error owner remains responsible for its lifetime.

Estimate110–160 changed lines; pause/reopen before material growth (>210).
Return exact resulting capture interface/slot contract to main before phase2.

### Phase1 estimate reopening and final-checklist correction

The paused patch is216 changed lines. Main read its whole diff: existing capture
and upload helpers are parameterized, finite root inverse/descendant ownership
are added, and optional-pair cleanup reuses native free owners. Renaming local
Ranger-only variables accounts for part of the growth; no second model/cache/
mesh owner was added. Keep this incremental design and reopen phase1 to a240-line
maximum before refinement; combined650-line reopening bound remains unchanged.

The [final Astra disposition](final-checklist-refresh-2.0-review.md) identifies
source/target policy conflation. Primary r_alias.c:5262 gates ATTACH_HAND on the
selected target, resolving exact QBJ source by key/digest. Existing implicit
source admission accepts RANGER policy. Model-owned extraction must recognize
the verified source geometry independent of a future selected target's policy;
apply ATTACH_HAND at frame attachment preparation, not source extraction. Remove
that misplaced source-policy condition. Finish the paused stale helper rename,
then return source-only refinement and exact phase2 interfaces for main review.
Tests/builds remain deferred; this reopening does not accept the patch.

## Phase2: staged immutable attachments and independent bounds

After phase1 main source review, writes r_vrik_render.c/h only. Keep canonical
Ranger animation source. Main-thread staging may borrow exact optional source
model/geometry/skeleton and store validated bind matrices by value to avoid
another rig-live-pointer lifetime. Revalidate admission/digest/current model
identity/sockets/material/static-view/AS readiness during preparation. Do not
load/rehash on workers. Source stage miss does not reject the selected body.

Shared fixed record type: geometry, affine, validity and conservative bound;
candidate/prepared expose up to two records with count. Ordinary Ranger emits
one with existing behavior. QBJ emits zero or two, fully checked before pair
publication; no partial candidate from one successful attachment. Clear props
and muzzle before optional attempt. Preserve C13 death/corpse no-live-tracking,
root or muzzle policy and frame compaction/rebinding owners.

For source anchor bind S: rotate basis using source_context.rotation, map its
origin using source_context.forward. Feed S as pose and bind to existing socket
helper, target accepted palette/bind (humanoid reference when applicable), and
identity source_prop_pose. Compose returned rigid socket with
source_context.forward * source_root_bind; root-local views then reproduce
primary full canonical bind points without scaling prop by target body twice.
Finite matrix and each view bound checks precede publication. Union both prop
bounds into existing conservative body bound. No animation/rig algorithm rewrite.

Estimate170–230 changed lines. Reopen >290/new publication owner.

## Phase3: native consumers

After phase2 main review and C07 draw worker releases r_alias.c, writes r_alias.c
and r_brush.c only. Iterate bounded records at existing shared helpers: original
body transform plus each prop affine, source skin/glow, separately transformed
lighting, static identity palette/zero poses; raster, co-op fill/masks/rings and
ShowTris. Prevalidate complete pairs so consumer failure cannot show one QBJ prop.
Keep all ring masks before all outlines. Wheel/input/gameplay matrices unchanged.
TLAS count and emission both use identical bounded pair admission/iteration.
Native GLMesh shared prop BLAS discovery stays unchanged unless source proves
a missing seam; seek main approval of reopened plan before changing its owner.

Estimate110–160 changed lines; reopen >210. Combined rough390–550; reopen on
material growth (>650), duplicate policy/cache, new upload/animation layer or
unexpected adjacency. Coding delegates edit only precise current-phase files,
no tests/builds/lint/probes/assets/docs/staging/commits.

## Final consolidated acceptance

Actual selected ATTACH_HAND avatars show both QBJ anchors through animation,
accepted left dominance, deaths/queued corpses, skin/glow, conservative either-eye
culling, co-op overlays/ShowTris and offscreen ray shadows. Missing/wrong-digest
source, wrong socket, one-prop geometry/helper/readiness failure publishes neither
optional prop, no Ranger substitute/muzzle, and keeps body. Ordinary Ranger
attachment remains native. Verify mode/slot/model/reset lifetime and native ARM
client rendering in final software qualification; source integration is not proof.
