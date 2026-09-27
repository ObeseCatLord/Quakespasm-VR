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

The inherited opt-in calibrated humanoid policy is still a separate 2.0
migration gap: `R_AvatarRetargetHumanoid` exists, but frame preparation
currently calls the generic palette retargeter. Preserve the donor's target
lengths and tracked IK when wiring that policy into the selected-avatar path;
manual clips or the Alicia VRM spike do not supply equivalent behavior.

For performance qualification, measure full frame preparation by active
avatar count, rig size, desktop versus tracked pose, and stereo mode. If
retargeting becomes a material frame cost on large custom rigs or many peers,
first precompute stable bind inverses, local transforms, and semantic ownership
per admitted rig; keep the current per-frame pose transfer and GPU skinning.
Hand-authored desktop animation is an option for a particular avatar only if
its measured cost and content quality justify maintaining separate clips. It
cannot replace the live head/hand/FBT solve for tracked players.
