# Calibrated humanoid avatars on the Vulkan branch

The inherited `r_avatar_humanoid` control is not wired into 2.0. The donor
applies it only to complete custom humanoids, with `0` retaining generic
retargeting, `1` adding target-length limb IK, and `2` retaining calibrated
retargeting without IK. `Quake/r_avatar.c` already contains the donor's
calibration, retarget, height, and limb-solve functions. The Vulkan frame path
in `Quake/r_vrik_render.c` still always calls the generic retargeter.

## Architecture choice

| Route | Reused behavior | Missing boundary | Cost/risk |
| --- | --- | --- | --- |
| Extend the frame-owned avatar record | Existing Ranger animation/VRIK solve, custom MD5 admission, pure calibrated retarget math, Vulkan palette upload, raster and shadow consumers | Opt-in selection, normalized profile/presentation, calibrated floor contacts, raw tracked goals and target IK | Narrow adapter; one renderer and one selected-player owner |
| Copy the donor renderer or direct-VRM Alicia spike | Donor CPU skinning and one fingerprinted sample asset | Vulkan task/descriptor/BLAS integration and general asset admission | Duplicates render ownership and can move skinning back to CPU |

Use the first route. Manual desktop clips can be considered per avatar only
after measuring a material retarget cost; they cannot represent live tracked
head, hands, and FBT. A VRM importer would still need the same pose mapping
and should feed the existing Vulkan skinning palette rather than introduce a
second renderer.

## Required adapter boundaries

1. Stage the donor's custom-only `r_avatar_humanoid` policy and a normalized
   target profile with the frame's borrowed skeleton views. Rebase all staged
   pointers after publication, as the current rig views already require.
   Keep mode `0` and incomplete/virtual rigs on the current generic path.
2. Build one calibrated reference map and its presentation context for a
   selected complete humanoid. The donor normalizes head-to-average-ankle
   height while preserving manifest scale. Apply the same presentation to
   visible geometry, culling bounds, attached equipment, and ray shadows.
3. Recompute floor correction from the calibrated **reference skin**, not
   merely from the existing baked bind vertices. The donor skins contact
   vertices with calibrated bind matrices; 2.0 currently retains only baked
   positions and contact weights. Retain or derive the minimum extra
   influence data at model admission, with bounded memory and model-lifetime
   ownership. Cache the resulting floor correction by model and policy.
4. Transfer the canonical Ranger palette through
   `R_AvatarRetargetHumanoid`. For tracked players, use the raw pre-clamp
   head/hand and accepted hip/foot goals in the same source body basis used
   by the Ranger solve, then map into target space and run the existing
   analytic target-length limb solver. Pelvis authority, missing trackers,
   unreachable targets, and mode `2` must follow the donor.
5. Publish one complete selected-avatar record or fall back to the original
   Ranger record. The same result must drive both eyes, culling, equipment,
   raster, and shadows; neither animation policy nor tracking should mutate
   the authoritative network entity.

The smallest end-to-end proof is one admitted complete custom humanoid in
desktop and tracked VR, alongside an incomplete rig that remains generic.
Exercise modes `0/1/2`, head-only crouch, real and missing hip/feet, an
unreachable wrist, avatar switching and resource failure. Compare the
selected silhouette, feet, equipment, and shadow across both eyes and
desktop. Profile CPU frame preparation with several peers and larger rigs
before claiming a performance benefit. User headset testing remains separate.

The frame adapter now stages custom-only modes `0/1/2`, a normalized profile,
the calibrated reference floor, and one presentation shared by body and prop.
Mode `1` maps raw tracked goals through the Ranger pre-solve basis and solves
on target lengths; desktop mode `1` derives the support hand from the
calibrated dominant grip. Mode `2` omits target IK. Failed optional reference
data falls back to generic retargeting. Both Linux builds and the synthetic
avatar fixture pass. Asset-backed desktop and VR presentation, shadows,
tracking loss, and performance are not yet verified.

## Astra senior review disposition

The review endorsed the minimal adapter and found no P0 issue. It identified
three P1 boundaries: the cached floor value must come from the calibrated
reference skin and include scale/reference changes in its key; raw tracked
goals need the exact pre-solve Ranger body basis instead of already clamped
joint positions; and equipment must use the shared presentation and
calibrated wrist reference. All three are adopted above. The original MD5
influences are available while parsing the model, but the former retained
bind surface omitted them; 2.0 now retains a bounded optional copy for
custom humanoids. A failed optional copy leaves the generic avatar path
available. The integrated frame path still needs the asset-backed proof above.

The follow-up Astra code review found no confirmed P0/P1/P2 defect in the
integrated adapter. It retained custom-only admission, mode fallback, staged
pointer rebasing, mode-specific floor cache keys, the pre-solve tracked basis,
hip-over-head authority, and calibrated equipment/support-hand transport.
That review was read-only and did not establish in-game visual or timing
results.
