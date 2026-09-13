# Humanoid retargeting investigation

Inspected Quakespasm-VR at `e821a718` plus the existing Alicia experiment,
FPSloppa at `a873edb`, and the installed Anzu, Chino, Marmot Ranger, and QBJ3
packages. This investigation adds an offline diagnostic, not a runtime pose
change. Existing game installations and FPSloppa were read only.

## Recommendation

Keep Ranger's animation selection and action timing initially. Introduce a
shared humanoid pose adapter that preserves the target skeleton's lengths,
adapts reference poses/axes, then solves weapon and foot constraints on that
target skeleton. Both MD5 and direct VRM rendering should consume its output.
VR tracking should feed the same target solver with different constraints.

This is a tractable extension of the existing semantic rig and IK code. A new
animation library can improve motion style later, but will still need this
adapter. Manually authoring every animation for each avatar would address the
symptoms at the wrong level.

## Measured failure

`R_AvatarRetargetPaletteWithContext` in `Quake/r_avatar.c` transfers each mapped
joint's global rotation delta and **independent global positional displacement**
from Ranger. In simplified common coordinates:

```
target_joint_position = target_bind_position
                      + source_pose_position - source_bind_position
```

Consequently the vector between two posed target joints is not constrained to
the target's bind length. Rotations can remain rigid/orthonormal while the
hierarchical skeleton stretches or collapses. Mapping a T-pose to Ranger's
different reference silhouette compounds the problem.

The diagnostic parses the actual 26-joint Ranger mesh and all 143 animation
frames from the installed pak. It calls the current C retargeter and measures
eight arm/leg segments on six rigs. These are palette measurements **before**
optional renderer IK, not claims about final rendered positions.

For Alicia, excluding death slots 41–102:

| Segment | Bind length, Quake units | Min posed/bind | Max posed/bind |
|---|---:|---:|---:|
| Left upper arm | 6.689 | 0.230 | 2.236 |
| Right upper arm | 6.689 | 0.232 | 2.335 |
| Left forearm | 6.967 | 0.141 | 1.823 |
| Right forearm | 6.967 | 0.621 | 1.931 |
| Left thigh | 11.628 | 0.972 | 1.136 |
| Left shin | 13.446 | 0.796 | 1.006 |

The worst right-upper-arm extension is frame 131, an axe attack. This is not
solely a death-animation artifact. The four installed custom packages have the
same measured arm/leg bind lengths as Ranger, within export rounding. Their
different visible shapes therefore do not test arbitrary skeletal proportions.
Ranger itself has small authored frame-to-frame length changes (roughly
0.965–1.014 for arms); the much larger Alicia changes are not inherent to those
animations.

A diagnostic alternative keeps the retargeter's global rotations and Hip
motion, but reconstructs other joint positions hierarchically from target
bind-local offsets. Across all six rigs and all frames, maximum measured limb
length error is below 0.006%. Assertions also check Ranger semantic poses
reproduce their source within 0.001 units/matrix component. The executable ran
under AddressSanitizer and UndefinedBehaviorSanitizer without errors.

This alternative is **not** a finished retargeter: it does not align reference
silhouettes, guarantee hand contacts, distribute spine/twist motion, or validate
visual quality. Keeping offsets only isolates and removes the stretching
mechanism. Actual helper chains need additional tests beyond the eight measured
semantic segments.

## Why the existing IK did not fix it

`R_VRIKRefineArmPosition` measures lengths from the incoming posed palette.
For Alicia those lengths are already distorted. Its analytic path also permits
up to `VRIK_ARM_MAX_STRETCH` (1.10), and can reject unreachable targets.
Enabling `desktop_refine` improves hand placement but does not restore the
authored body. Custom v1 profiles leave that option off by default.

The renderer already contains useful components: semantic arm/leg mappings,
two-bone solvers, actual-parent-chain solvers, pole policies, endpoint orientation
handling, desktop weapon/support-hand repair, tracked hip/foot targets, and
atomic fallback. Some policies are tailored to particular monster rigs. Keep
their established behavior while introducing an explicit humanoid retarget mode.

The current tracking route solves a canonical Ranger palette first, transports
it, then refines the target. Long term, pass original calibrated tracking
targets directly to target-space IK: a Ranger reach clamp should not determine
what an avatar with different reach is allowed to do. Existing confidence,
prediction, dropout, coordinate conversion, and pose caching remain useful.

## Proposed shared pose pipeline

1. **Build an anatomical rig description on load.** Resolve VRM humanoid
   semantics or MD5 manifest mappings. Cache physical parent paths, bind-local
   transforms, limb lengths, bend planes, foot soles, and wrist/palm frames.
   Handle optional upper-chest, toes, fingers, and twist/intermediate bones.
   An extra spine link needs distributed rotation; an off-axis intermediate
   limb link cannot safely be treated as a direct two-bone chain.

2. **Separate skin bind pose from retarget reference pose.** Calibrate both
   rigs to a common anatomical reference (for example T-pose), including bone
   roll, using semantic bone directions and bend planes. Store correction
   rotations instead of overwriting the original inverse binds. A virtual
   normalized humanoid rig can drive the authored skeleton through these
   corrections. Merely copying local quaternions or mapping bone names is
   insufficient. Allow a small per-avatar correction profile for ambiguous
   palms, perfectly straight limbs, or unusual rigs.

3. **Normalize size uniformly.** Use a robust anatomical height, preferably
   foot sole to eye/head with a defined allowance, and an explicit override.
   Hair, hats, and carried accessories should not determine scale. Keep all
   target limb proportions. Scale root/hip motion separately using relative
   leg/hip height. Cosmetic scale does not change the movement hull. Do not
   let clip root motion move the network entity a second time.

4. **Retarget motion with fixed offsets.** Transfer anatomical rotations
   through reference corrections, preserve target bind-local translations,
   and apply normalized root/pelvis translation. Reconstruct the hierarchy.
   Unmapped accessory branches initially follow their authored parents.
   Preserve source knee/elbow bend intent where stable, with anatomical pole
   fallback near straight limbs. Keep an explicit exceptional policy if a
   particular source clip intentionally needs squash/stretch.

5. **Solve contact constraints on target lengths.** Use non-stretching reach
   bounds by default. For desktop weapons, place one shared weapon transform
   from action intent, aim, and body proportions; derive both wrist targets
   from that weapon's grip sockets. Adjust shoulder/clavicle and modest torso
   posture before conceding reach. Move the cosmetic weapon closer if possible;
   release or relax the support hand when necessary. Do not independently move
   two hands away from an otherwise fixed weapon and call that a valid grip.

6. **Preserve floor contact and motion phase.** Retarget swing-leg motion, but
   keep a planted foot at its contact during stance and let pelvis height
   accommodate target leg reach. Use bounded floor queries and foot orientation
   offsets. Blend contact weight through takeoff/landing and release locks on
   jumps, teleports, death, or tracking loss. A length-preserving FK pass alone
   will still slide feet when source stride and actual movement differ.

7. **Finish wrist orientation and fingers, then skin once.** A hand bone's
   origin is usually a wrist, not a palm grip. Use a wrist-to-palm transform and
   a per-weapon primary/support socket. Optional generic finger curls should
   bend in normalized finger-local axes. Missing fingers can remain rigid.
   Renderer-specific meshes, materials, and normals come after the shared pose.

Desktop and VR need different constraint priorities. In desktop play the weapon
and shoulders can move cosmetically to preserve the grip. In VR the tracked
dominant hand/weapon should remain authoritative; use calibrated avatar scale,
pelvis/torso compensation and bounded visual reach, then explicitly tolerate a
residual when the user's proportions or pose cannot fit. Three trackers do not
uniquely determine a full body pose, so animation/procedural motion remains the
prior for untracked joints. Physical two-hand tracking and a fixed support
socket can conflict; use an explicit soft grip rather than stretching a limb.

## Weapon and animation integration

Use one profile per weapon/action class, not per avatar/clip combination:
primary palm socket, optional support socket, wrist orientation, grip style,
body-relative hold position, and recoil/swing intent. Estimate avatar-specific
palm offsets from finger semantics where available. A small manual correction
is a reasonable fallback.

For QBJ3 retain the existing native-equipment attachment policy and explicit
gameplay frame semantics. The current slots are run 0–11, stand 12–28, pain
29–40, death 41–102, gun attacks 103–118, and axe attacks 119–142. Replacing
motions requires mapping these states/phases and preserving shot/melee timing;
it is not just loading an FBX. Retarget the swing trajectory and bend intent,
allowing smaller cosmetic reach on a smaller body. Coop tolerance makes that
tradeoff practical without changing authoritative attack behavior.

Manual visual tuning is valuable for the Ranger reference pose, weapon sockets,
grip styles, and contact/event annotations **once per source/weapon**, followed
by multi-avatar validation. It should produce reusable calibration metadata,
not hundreds of avatar-specific frame edits.

## FPSloppa reference

The useful parts are concrete and relatively small:

* `addons/vrm/vrm_utils.gd`: `perform_retarget`, `skeleton_rotate`, and
  `apply_mesh_rotation` rename humanoid bones, normalize axes, and compensate
  skin bind data. This importer code is MIT licensed. It illustrates why a
  reference-frame change must be coordinated with skinning.
* `deathmatch/avatars/rig.gd`: scales a model to 1.70 m from mesh bounds;
  `build_animations` generates only Hip-position bob tracks for idle/walk/run.
* `deathmatch/avatars/pose.gd`: resets relevant rotations to rest, generates
  procedural foot trajectories and floor targets, and solves arms/legs by
  rotating joints. Reach clamps below total limb length rather than stretching.
  It also handles tracker overrides, controller-to-wrist axes, local finger
  curls, pelvis-derived knee poles, and distance-based solver frequency.
* `deathmatch/art.gd`: explicit model-space palm anchors and a separate weapon
  aim basis. This is a useful separation for Quake's equipment handling.

It is not evidence of a sophisticated imported locomotion-library retargeter.
There are fixed arena-space hand/foot/pole offsets and a fixed palm-to-wrist
offset; simple floor correction is not a complete planted-foot constraint.
These choices suit its normalized sample bodies but should not be copied as
universal anatomy. I inspected source/tests, not a running FPSloppa session.

The checkout also supplies `vrm/sample_d.vrm`, `sample_f.vrm`, and `sample_g.vrm`
(144, 164, and 171 nodes; all VRM0). They are useful additional candidate test
bodies. They have not been imported by this Quake experiment. Keep sample-model
licenses distinct from the MIT importer license.

## Alternative motion sources

* [Quaternius Universal Animation Library](https://quaternius.com/packs/universalanimationlibrary.html)
  explicitly lists CC0, a humanoid rig, locomotion and gun/combat actions, and
  GLB/FBX formats. It is the most promising first candidate for a redistributable
  shared motion pack. The full advertised library and free download tier are
  not identical; inspect the selected download's actual clips before planning
  coverage. No pack was downloaded or its motion quality evaluated here.
* [Kenney Animated Characters Protagonists](https://kenney.nl/assets/animated-characters-protagonists)
  lists CC0. It is a valid source to inspect, but this investigation did not
  establish sufficient weapon/swing coverage for QBJ3.
* [Adobe's Mixamo FAQ](https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html)
  permits royalty-free game use. That is not equivalent to CC0 or unrestricted
  source-asset redistribution. Adobe's [distribution guidance](https://community.adobe.com/questions-696/mixamo-faq-licensing-royalties-ownership-eula-and-tos-589400?lang=fr)
  excludes distributing raw animation/character files. Prefer CC0 for a motion
  library intended to ship as editable assets with this open-source project.

The proposed separation agrees with [Godot's retargeting documentation](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/retargeting_3d_skeletons.html):
normalize rest conventions, remove non-root positional tracks, and normalize
motion translation. [Epic's biped retargeting guide](https://dev.epicgames.com/documentation/unreal-engine/retargeting-bipeds-with-ik-rig-in-unreal-engine)
likewise distinguishes chain mapping and matching reference poses. These are
architectural references, not dependencies to add to Quake.

## Suggested implementation stages and validation

First implement opt-in humanoid reference calibration and fixed-length FK,
retaining Ranger action timing. Compare Alicia and the four existing packages
through every action, and include additional VRMs plus synthetic short-arm,
long-leg, and intermediate-joint rigs. Then add shared grip sockets and target
IK; then stance contacts/pelvis adjustment and direct tracking targets. Only
afterward compare a CC0 motion pack against Ranger as an alternate source.

Acceptance should include bind reconstruction, invariant target limb lengths,
correct bend direction, grip position **and orientation** error, unreachable
target behavior, foot drift during stance, transitions, and equipment timing.
Render multi-angle action sequences for visual review, especially extreme aim,
axe swings, crouch, stairs, jump/landing, and death. Existing monster behavior
and two-eye pose-cache consistency need regression coverage. This turn's numeric
probe does not establish those visual or VR outcomes.

## Lighting follow-up

The original MD5 spike reverses source triangle winding for `GL_CW` rendering.
Both MD5 normal generators then use `(b-a) cross (c-a)` on that reversed order,
whereas direct VRM drawing retains authored normals. On Alicia's source bind
geometry, the area-weighted dot between face normals and averaged authored
normals is +0.990334; reversing the order makes it -0.990334. This identifies
a normal-sign convention mismatch worth testing separately, beyond expected
decimation/smoothing differences. The runtime normal pass also lacks the
loader's position-welding step. No normal/rendering code was changed here,
and this calculation is not a vertex-matched comparison of the decimated mesh.

## Reproduce the numeric probe

Generate asset-derived input outside the checkout, then compile against the
current retargeter. This harness reuses matrix helpers from the existing fixture
but does not run that fixture's full test suite.

```sh
mkdir -p /home/s/.local/share/quakespasm-vr-spikes/alicia/retarget
python3 experiments/alicia/retarget_probe.py \
  /home/s/Downloads/quakespasm_straight/id1/pak0.pak \
  /home/s/Downloads/quakespasm_straight/player_models \
  /home/s/.local/share/quakespasm-vr-spikes/alicia/base/player_models/alicia \
  /home/s/.local/share/quakespasm-vr-spikes/alicia/retarget/retarget_input.h
nix develop --command bash -c '
  cc -std=gnu99 -D_DEFAULT_SOURCE -IQuake $(sdl2-config --cflags) \
    -I/home/s/.local/share/quakespasm-vr-spikes/alicia/retarget \
    -fsanitize=address,undefined -g \
    experiments/alicia/retarget_probe.c Quake/r_avatar.c -lm \
    -o /home/s/.local/share/quakespasm-vr-spikes/alicia/retarget/probe && \
  /home/s/.local/share/quakespasm-vr-spikes/alicia/retarget/probe > \
    /home/s/.local/share/quakespasm-vr-spikes/alicia/retarget/results.csv'
```

The parser deliberately supports only the inspected all-components-animated
Ranger MD5 layout and scale-1 manifests. Measurements sample authored frames,
not interpolated subframes or tracked poses. Generated skeleton/frame input and
the full CSV remain in the private local artifact directory; no model or motion
assets were added to the repository.
