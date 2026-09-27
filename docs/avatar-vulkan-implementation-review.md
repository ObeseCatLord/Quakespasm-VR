# Vulkan avatar implementation: Astra senior review

Date: 2026-09-26. Branch: `2.0`. Review baseline: `c9b38b18`.
The inherited `master` renderer is the behavior reference. This review covers
the built-in avatar vertical path; the concurrent custom-package loader was
outside the reviewed diff.

The frame-owned render view remains the right boundary. It keeps the original
player entity as the render/BLAS owner, solves the canonical Ranger animation
and tracking before retargeting, and gives raster, culling, and ray shadows the
same selected geometry and palette. Missing assets fall back to the original
player model. Astra found no confirmed task-staging race or raster/TLAS
transform mismatch. Replacing vkQuake's renderer would add unnecessary state.

| Priority | Finding | Disposition |
| --- | --- | --- |
| P1 | Generic retargeting lacks inherited target endpoint, posture, and leg repairs (notably Dog and Fiend). | Partially addressed in `a45aa588`: desktop Dog/Fiend posture and arm repair and Vore outer-knee repair. Tracked endpoint and remaining physical-leg refinements are open. |
| P1 | Authored monster equipment still appears, and the player's equipped weapon is not attached. | Open: filter body indices and add frame-owned attached parts shared by raster, overlays, bounds, and BLAS. |
| P2 | Hip alignment omits the inherited static bind-floor correction. | Implemented in `a7e34d9d`: avatar-only retained bind geometry, source/target contact filtering, cached presentation offset. Asset-backed visual validation is open. |
| P2 | Presentation rotation left alias lighting in Ranger space while normals stayed in target space. | Fixed in `a1af91dc`: rotate the shade vector into target space. |
| P2 | Out-of-range Ranger run/stand fallback advanced in visible 100 ms steps. | Fixed in `c39168a1`: interpolate adjacent canonical poses without mutating entity lerp state. |
| P3 | Transparent alpha-sort distance and water classification still use original-model bounds. | Implemented in `610095a7`: use the prepared animated bound after palette preparation. |

Static rig resolution and repeated consumer validation are candidates for
centralization once the custom path settles. Preserve distinct main-thread
admission and frame-owned GPU publication; they have different lifetimes.

## CPU retarget cost and asset alternatives

An isolated `-O2` Linux timing probe using the existing synthetic 19-joint
avatar fixture measured about 2.6 microseconds per call to
`R_AvatarRetargetPalette`, or 4.2 microseconds including two
`R_AvatarResolveRig` calls. It ran 200,000 iterations on this machine; it did
not include model-specific posture/IK refinement, full frame preparation,
GPU skinning, or rendering. These numbers are a scale check, not an in-game
budget or a guarantee for larger rigs. Profile actual selected avatars and
multiple peers before claiming a frame-time improvement.

The inherited direct-VRM Alicia path is explicitly an asset-fingerprinted
experiment (`quakespasm-openvr/Quake/r_alicia_spike.c`), not a general VRM
importer. Converting a selected body to VRM would still require mapping
canonical/tracked motion into its skeleton. Hand-authoring desktop animations
could avoid desktop retargeting for that particular body, but tracked VR motion
would still need a pose solve. The current branch now reuses same-frame
validated rig mappings at the existing staging boundary, eliminating the
second skeleton/semantic-name resolution in palette preparation. No new
animation format or cross-frame cache is justified by the available timing.

The smallest remaining implementation proof is two tracked peers and one
desktop peer using the same equipped alternate, with independent poses,
reference posture and floor contact, matching visible/shadow silhouettes,
and safe frustum-edge behavior. Also exercise avatar switching, tracking
loss, missing/malformed assets, and task rendering on/off. Synthetic parser
fixtures and compilation alone cannot establish that behavior; live headset
testing remains with the user.

## Follow-up animation performance decision (2026-09-27)

Keep the current per-frame Ranger-to-selected-rig palette retargeter for the
2.0 implementation. It runs once per selected player in frame preparation;
the resulting matrices feed vkQuake's Vulkan skinning and are shared by the
eyes and render consumers. The isolated 19-joint measurement above is too
small to justify hand-porting the action library or reviving a separate CPU
renderer. The 2.0 custom avatar path currently admits MD5 meshes; the donor's
direct-VRM Alicia path is an asset-specific OpenGL/CPU-skinning experiment, not
a reusable general VRM avatar runtime. A VRM importer would still need the
tracked-pose mapping and would need to use the existing Vulkan palette/skin
path to meet the performance goal.

The inherited opt-in calibrated humanoid policy was wired into the Vulkan
avatar frame path in `102e201c`. Complete custom humanoids can use calibrated
retargeting, target-length tracked IK, and the desktop support-hand solve;
mode `0` and incomplete rigs retain generic retargeting. The integrated path
builds and passes its synthetic fixture, but asset-backed presentation and
timing remain unverified. Manual clips or the Alicia VRM spike do not supply
equivalent tracked behavior.

An additional isolated `-O2` probe of that calibrated path used the same
synthetic 19-joint fixture for 200,000 iterations. Building its presentation
context and humanoid calibration took about 3.6 microseconds per avatar;
`R_AvatarRetargetHumanoid` took about 1.6 microseconds. Together that is about
5.2 microseconds per avatar, or about 0.08 ms for sixteen avatars if the cost
scaled linearly. This excludes rig resolution, animation and tracking solves,
target IK, equipment, upload, and rendering; it is not an in-game frame-time
measurement. Calibration currently runs when each avatar is staged each frame,
while the retargeted palette is prepared once per player and reused by both
eyes. The profile's reference floor correction is already cached separately.

The tracked humanoid limb solver also rotated the upper arm/leg, lower
arm/leg, and endpoint subtrees by walking each physical joint's parent chain
three times. A bounded local branch mask now propagates those three subtree
memberships in one parent-ordered pass, using the existing rig-admission
invariant. In an isolated `-O2` 10,000-iteration probe, a synthetic 256-joint
rig with a long hand-descendant chain fell from about 141 to 9 microseconds
per limb solve; a 19-joint rig stayed near 0.4 microseconds. Old and new
palette hashes matched in both cases. The fixture now also checks a full
256-joint branch, an unrelated interleaved prop, and preserved physical link
lengths. This is a worst-case CPU microbenchmark, not a measured in-game FPS
gain or proof of a typical avatar's cost.

For performance qualification, measure full frame preparation by active
avatar count, rig size, desktop versus tracked pose, and stereo mode. If
retargeting becomes a material frame cost on large custom rigs or many peers,
first precompute stable bind inverses, local transforms, and semantic ownership
per admitted rig; keep the current per-frame pose transfer and GPU skinning.
Hand-authored desktop animation is an option for a particular avatar only if
its measured cost and content quality justify maintaining separate clips. It
cannot replace the live head/hand/FBT solve for tracked players.

The generic Vulkan path now reuses the presentation context already built
during that frame's avatar staging instead of recomputing its bind-only body
bases during palette preparation. A fixture compares the old bridge and the
staged-context output after floor correction; both produce the same palette.
This removes redundant setup, but no in-game frame-time gain is claimed.

The staging path now also caches resolved semantic joint indexes per admitted
avatar ID, keyed by the source/target models, their retained skeletons, and the
target profile. It still validates both skeleton views on each use and binds
each player's staged rig to that player's current views; admission reset clears
the cache. This avoids repeating the name-to-joint resolution for every peer
every frame without changing the canonical animation or live tracking solve.
The Linux build passes. The earlier 4.2 versus 2.6 microsecond isolated probe
suggests the possible scale, but no full-frame improvement is claimed.

The same admission cache now stores the generic and opt-in humanoid bind-only
presentation contexts and the humanoid calibration. Each staged player copies
the appropriate immutable values before its own floor correction and fresh
tracked/animated pose. A change in admitted model, retained skeleton, profile,
or game directory invalidates these values with the rig mapping. Failed
humanoid admission still falls back to the generic path. The isolated
3.6-microsecond calibration/setup figure above is an upper-bound motivation,
not an in-game measurement of the new cache's benefit; the Linux build passes.

`scr_speeds 3` now displays `avatar prep cpu` for the complete frame-owned
avatar preparation task. The timing runs only in that profiler mode. It includes
all active player candidates, canonical animation or tracking, retargeting,
culling bounds, and palette publication, so it is a useful in-game budget but
does not isolate the retargeter alone. Compare it with the total CPU frame time
on the same scene before considering per-avatar animation replacements.

The tracked Dog/Fiend refinement now accepts the floor-corrected presentation
context already staged by `R_VRIKRenderStageFloor`. The legacy entry point still
builds its own context for callers without a staged frame. This removes one
bind-basis rebuild per tracked animal without changing the solver or Vulkan
palette ownership. The Dog lower-body fixture compares the two routes after a
nonzero floor correction, a failed-limb rollback, and a tracked-hip turn; their
resulting palettes match. The Linux debug build passes. This is source-level
redundancy removal, not a measured frame-time or headset performance gain.

The generic retargeter's owned semantic joints now write their already-global
pose directly. They no longer build and discard an authored bind-local pose or
convert the solved global pose to a parent-local pose and immediately back.
The presentation rotation inverse is also calculated once per palette. Unmapped
children still use their authored bind-local transforms. The retarget and
lower-body fallback fixtures pass under ASan/UBSan, and the Linux debug build
links. A local isolated `-O2` probe on this machine measured 2.36 to 1.52
microseconds per 19-joint transfer and 11.4 to 10.6 microseconds for a synthetic
256-joint chain (300,000 and 20,000 iterations respectively). This measures
only the generic transfer, not full avatar preparation or in-game FPS.
