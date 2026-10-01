# C14 phase2: reopen size/reuse decision before further edits

2026-10-01. Main uses senior-review for a real architecture estimate reopening.
Phase1 committed19e2c6b3. Phase2 worker is stopped at392 changed lines
(308 additions/84 deletions), above290 pause threshold. Consumer phase3 remains
unimplemented; projected217+392+110–160=719–769 exceeds combined650 threshold.
No tests/builds/compiler/lint/probes/assets execution until all implementation.

## Verified environment and evidence

Writable established2.0 checkout `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`.
Readonly primary `../quakespasm-openvr`, pin51b452c018273647dcf94f4628a370267ff8fa91.
User-owned dirty docs/migration-2.0.md untouched and not acceptance evidence.
Paused writes only Quake/r_vrik_render.c/h; packaging Luna independently owns
Packaging/Linux/sources.json,Dockerfile,build-native.sh. Read-only this review.

[verified: main full current diff] Two candidate/prepared attachment records
replace single fields. One embedded staged-equipment value contains borrowed
model/skeleton/views and copied root/anchor/context matrices, not a resource cache.
Source identity/digest/current readiness checks use existing custom_models,
native admission and R_VRIKRenderValidatePropView/GLMesh_AvatarPropBLASReady.
Original model mesh/texture/upload/free/BLAS owners unchanged. Frame entry owner
and compaction remain existing C13 owner. Current r_alias/r_brush consumers still
refer to old fields; source is intermediate, unaccepted and not compile-ready.

[verified: main source] Main read R_AvatarBuildAttachedPropTransform:467:
orthonormal input/result requirement means scaled source presentation cannot
be passed as prop pose. Pair implementation builds source anchor basis with
rotation-only and maps its origin with full forward, gets rigid socket via
identity prop pose, composes socket * source.forward * root.bind. Phase1 meshes
are source-root-local, not original absolute points. This matches reviewed plan
and primary full-bind-point/socket semantics; numeric output unexecuted.

[verified: main source] New R_VRIKRenderMultiplyAffine duplicates general affine
multiplication using double/finite checks (~29 lines); existing mathlib.c:417
R_ConcatTransforms performs identical3x4 affine composition in float, reads its
non-const inputs and requires distinct output. R_AvatarMultiply is private static
in r_avatar.c, not an existing public API. New source-anchor helper (~29 lines)
needs rotation-only basis/full-origin mapping; MakeAttachment moves existing
Frobenius/translation bound math into a shared helper for Ranger and both props.

[verified: main current diff] StageEquipment lost its selected-target early gate
while source ATTACH_HAND requirement was removed. It now resolves/loads the
optional QBJ model for every selected avatar in QBJ, including native equipment
that never consumes props. Main lean: gate by effective selected target rig
profile, including normalized humanoid policy, before optional lookup. Dispatcher
already uses target_rig->profile. Do not reintroduce source-policy conflation or
drop missing-source body fallback. Body selection's existing custom/frame
resource paths remain unchanged.

[verified: main source] AlternateCandidate clears muzzle before attachment and
only attachment publishes one; ClearAttachments+clear muzzle at dispatcher is
redundant but not a native-muzzle regression. Equipment policies have exactly
RANGER and ATTACH_HAND. Paused dispatcher uses comma-expression fallback for
zero props/body-visible; explicit condition is clearer. Ranger body failure on
missing required native prop retains original convention; QBJ optional pair must
never reject body. Death/corpse independently clears root/muzzle.

## Mostly-worked decision

Lean: retain fixed two records and optional staged borrowed source at current
native boundaries, remove unnecessary work/helpers/format churn, and explicitly
revise estimate if remaining growth reflects real producer/lifetime adaptation.
This is solo project, not a new avatar architecture. Smaller line count is not
permission to remove identity/finite/AS/bounds/pair safety.

Alternatives: (a) reuse R_ConcatTransforms behind a finite/non-alias wrapper and
retain source-anchor helper; (b) export a new shared r_avatar affine API; (c)
retain standalone double helper. Prefer(a) if failure semantics remain acceptable;
reject(b) unless there is another demonstrated caller/required robustness need.
Rejected: native source-body hidden on optional miss, merging different sockets
into one transform, per-frame vertex streams/new cache/importer, production
temporary old-field duplicates, broad Ranger rewrite merely to ease delegation.

Review questions: Is this architecture still minimal? Which new helpers/guards
are necessary versus existing reusable owners? Are there real math/policy/lifetime
defects, particularly source scaling/target normalization and staged owner copies?
Recommend minimal corrections and justified new phase2/combined bound. Do not
pre-order deep math spec if you rank another defect higher. Unknown output/runtime
behavior remains final qualification, not evidence to rewrite adjacent systems.

One local Astra xhigh, verify then critique, read-only, no tests/probes/SSH/
telemetry/branches/agents/edits. Output<=1500words prioritized adopted-design
recommendation, exact file-line evidence, deletion/reuse opportunities, scope/
estimate reopening, any genuinely human decisions and final qualification limits.
Main will spot-check claims, synthesize disposition and update plan before coding.
If evidence/budget insufficient, identify exact remaining decision, not a new scope.
