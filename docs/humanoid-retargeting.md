# Calibrated humanoid avatar policy

The opt-in `r_avatar_humanoid` policy gives complete custom humanoid rigs
anatomical reference calibration and target-length animation/IK. It is shared
by the normal custom MD5 path and the optional Alicia direct VRM renderer.
The latter still uses the converted package as its skeleton/selection bridge;
this change does not add a general VRM importer.

## Controls

| Value | Behavior |
|---|---|
| `r_avatar_humanoid 0` | Existing retargeting and model-specific IK; default |
| `r_avatar_humanoid 1` | Calibrated humanoid pose, normalization, target IK |
| `r_avatar_humanoid 2` | Calibrated pose and normalization, without target IK |

The policy applies to custom avatars with 19 distinct, real humanoid semantics.
Incomplete/virtual mappings retain the previous policy. Built-in Ranger and
monster profiles retain their existing behavior. Selection and gameplay frame
timing, movement hulls, attack traces, and network protocol are unchanged.

The Alicia launcher enables mode 1. F5 switches MD5/direct VRM, F6 toggles
equipment, and F7 switches legacy/new retargeting. `r_alicia_pose 1` remains a
bind-pose diagnostic; use `r_alicia_pose 0` for these comparisons.

## Implemented

`R_AvatarBuildHumanoid` derives per-semantic reference frames from bind segment
directions and the common body frame. It calibrates source joint axes to target
skin-bind axes, independently of the exporters' quaternion conventions. Feet
use body facing because toes are not required. Hands use forearm continuation
because finger/palm semantics are not yet part of the custom package schema.

The reference orientation is deliberately separate from the mesh bind. Original
joint-local weights and inverse binds are never changed. Bind input therefore
produces the target in the source reference silhouette, rather than reproducing
the target's original T-pose. Extra physical joints retain their local bind
transforms under the calibrated mapped parents.

`R_AvatarRetargetHumanoid` transfers calibrated global rotations and reconstructs
joint positions from authored local offsets. Hip displacement is scaled by the
target/source leg-length ratio. The operation validates matrices and publishes
the completed palette atomically. This removes the independent joint-position
transfer responsible for extreme stretching in the previous Alicia result.

The renderer normalizes head-to-average-ankle height uniformly, preserving the
manifest's `scale` as a multiplier. Hair, hats, and props do not affect this
normalization. The existing floor anchor uses the calibrated reference skin,
not the original T-pose. This is reference anchoring, not stance-foot locking or
a ground-contact solver.

`R_AvatarSolveHumanoidLimb` solves arms or legs by rotating complete physical
subtrees. It preserves their offsets, including interposed links, and clamps
unreachable goals without stretching. It restores endpoint orientation along
with finger/toe descendants and returns target-space residual distance (negative
for invalid input). The animated bend plane supplies the pole, with deterministic
fallbacks for straight or antiparallel directions. It is an analytic two-segment
solver, not a general articulated-chain optimizer or anatomical joint-limit solver.

For desktop equipment, the dominant wrist follows the calibrated animation.
The attachment code uses the calibrated reference wrist, so the weapon does
not inherit an unrelated T-pose correction. The support wrist targets the
source's relative hand position transported through that same attachment;
orientation is calibrated too. Native QBJ3 hand/back attachment also uses the
new references, including the dominant-left reference selection.

For tracked avatars, positions are converted from the raw head/hand and supplied
hip/foot targets using the same source body basis as the existing renderer.
Their positions are not taken from Ranger's reach-clamped joints. A supplied
pelvis target translates the whole body; otherwise the head target supplies
that translation. Hand/foot IK then solves on the target's lengths. Source
animation foot goals remain available when foot trackers are absent, so a
head-only crouch bends knees rather than translating feet through the floor.
These goals are resampled each frame and are not world-space stance locks. Source
solved orientations and the existing equipment/tracking orientation conventions
are still used. Confidence/presence gates and the existing tracking sampler
remain in place.

## Validation

* All 143 installed Ranger frames on Alicia, Ranger, Anzu, Chino, Marmot Ranger,
  and QBJ3 passed the offline C probe against the actual new implementation.
  The CSV retains legacy min/max ratios alongside new-policy length error.
* Synthetic tests cover different proportions, non-unit presentation scale,
  an off-axis intermediate arm joint, 16 body rotations, reachable and
  unreachable endpoints, endpoint orientation, physical link lengths, virtual
  mapping rejection, and invalid-input rollback.
* A renderer-boundary test supplies raw wrist, hip, and foot targets that differ
  from canonical endpoints. It checks contact positions, preserved body lengths,
  pelvis authority, unreachable-wrist residual, and source-palette immutability.
  A head-only crouch also keeps the animation's foot heights while lowering
  the head, without introducing limb stretch.
* Existing targeted retargeting and renderer-boundary fixtures passed with their
  ordinary flags and ASan/UBSan. All 14 default fixtures passed under ASan/UBSan.
  The full build required `-Wno-error=array-bounds` for existing GCC diagnostics
  in the unchanged `vrik_lowerbody_fixture.c`; other warning errors remained
  enabled. No fixture/compiler-warning repair is included in this change.
* The experimental and normal Linux engine configurations built. In-game
  desktop front/side and firing screenshots were inspected. An independent
  second client displayed Alicia through ordinary remote-player substitution in
  both direct VRM and MD5 modes, with preview substitution disabled.

Private asset-derived inputs, logs, CSVs, and screenshots are under
`~/.local/share/quakespasm-vr-spikes/alicia/retarget`. The updated experimental
binary is `../bin/quakespasm-alicia`; the previous one is retained as
`../bin/quakespasm-alicia-original`. Original game assets/configs were not changed.
No real headset/FBT session or multi-avatar performance measurement was performed.

## Remaining work

This is a usable comparison implementation, not the complete proposed pipeline:

* **Feet:** no stance-phase locks, floor queries, stride adaptation, or landing
  constraints. Source/reference anchoring alone can still leave feet sliding or
  above/below the surface during particular poses.
* **Hands:** no explicit per-weapon palm sockets or finger curls yet. The source
  animation supplies grip spacing. A support target outside reach produces a
  bounded hand pose and residual; it needs a smooth release/weapon-reposition
  policy to hide the gap. A large gun can still dominate a small body's pose.
* **Torso/FBT:** simultaneous head and pelvis targets do not yet drive a spine
  solver. Pelvis wins translation when both are present. Shoulder compensation,
  anatomical joint limits and twist distribution over optional spine/helper
  chains remain to be added. Raw positions bypass Ranger reach limits, but
  orientation calibration still passes through Ranger's existing tracked solve.
* **Style/accessories:** the visible arm/leg distortion is substantially reduced,
  but Ranger's hip/torso motion can strongly tilt Alicia's stiff skirt and hair.
  Those accessory chains have neither authored secondary animation nor springs.
  Reference calibration is inferred; there is no manual calibration-profile
  format yet, and ambiguous palms/feet may need overrides.
* **Scope/performance:** the runtime switch is intentionally opt-in and restricted
  to complete custom humanoids. Calibration is rebuilt with the live rig rather
  than kept in a persistent asset cache. The three additional FPSloppa VRMs have
  not yet been converted/rendered through this engine.
* **Lighting:** the earlier MD5 winding/normal-sign discrepancy is unchanged.

The next useful increment is grip calibration plus stance contacts and bounded
pelvis/torso adjustment, validated on more bodies. If Ranger's motion style is
still unsuitable after those constraints, compare a CC0 motion pack or a shared
humanoid variant of its action clips. Preserve QBJ3 action/event timing in either
case; avoid per-avatar animation libraries as the first remedy.
