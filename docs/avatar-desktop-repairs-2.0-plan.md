# Builtin desktop avatar repair adapter

Date: 2026-09-29. Implementation branch: `2.0`.
Behavior reference: readonly `quakespasm-openvr` master
`51b452c018273647dcf94f4628a370267ff8fa91`, `Quake/r_alias.c`.

Preserve the inherited desktop Ogre support grip and Shambler anatomical
arm/support grip repairs. They must feed the existing frame-owned palette,
attached weapon, culling and shadow consumers. Tracked poses remain authoritative.

## Verified state

- [verified: source comparison] The reference repairs Shambler's two real
  Shoulder/Upper/Lower/Hand chains before the optional desktop support grip
  (`R_VRIKRepairShamblerDesktopArm`, `R_VRIKRefineAvatarPaletteImpl`). It pins
  the raw wrist transform, restores authored link lengths and requires both
  intermediates to remain on the authored outward side.
- [verified: source comparison] Only Ogre and Shambler enable
  `desktop_support_hand`. `R_VRIKApplyDesktopSupportHand` transports the source
  left grip through the actual dominant-hand attachment, calibrates the target
  left wrist independently, requires a reached endpoint, and rolls back if
  the dominant wrist changes. Ogre uses its profile's analytic elbow pole;
  Shambler uses the physical shoulder-to-hand path.
- [verified: current source] `r_avatar.c` already owns rigid matrix/subtree
  helpers, the physical path solver and presentation maps. Its desktop early
  return skips these builtin repairs. `r_vrik_render.c` calls refinement once
  before attachment/bounds/GPU publication. No extra rendering pass is needed.
- [verified: current reference] The old fixed waist socket is inactive:
  only Dog/Fiend enable the profile flag, and the reference explicitly excludes
  desktop Dog/Fiend from that socket. Custom package parsing exposes no such
  profile fields. Do not add unused runtime machinery from a historical table.
- [unknown] Asset appearance and actual total CPU cost remain unqualified.
  This is a source adapter, not a performance claim.

## Adapter versus rewrite

Copy the reference Shambler arm and support endpoint math into `r_avatar.c`,
adapting the retained native skeleton view and existing rigid helpers. Reuse
the physical path solver for Shambler support, checking the original requested
endpoint so a clamped path cannot count as success. Copy only the desktop
analytic two-link portion needed by Ogre, using the existing body basis and
subtree operations. The calibrated custom-humanoid solver remains separate and
unchanged because its bind calibration and no-stretch contract differ.

Replacing the retargeter, adding CPU skinning, another animation format, an
equipment owner or Vulkan passes would duplicate working state. No such
incompatibility was found. Expected production scope: one file, roughly
300–400 copied/adapted lines, bounded stack snapshots and no heap allocation.
Reopen the design if this becomes a renderer or animation-system rewrite.

## Implementation stages

1. Add the reference anatomical Shambler repair using native matrix helpers.
   Snapshot/restore each optional arm independently if adaptation fails, and
   preserve the raw wrist transform including its descendants.
2. Copy the attached-prop support endpoint calibration. Reuse the prepared
   presentation context including floor correction, or build a context for
   existing callers without one. Validate source/target joints and palette
   before use. Require the original endpoint to be reached and the dominant
   hand to remain byte-identical; rollback the complete optional grip on failure.
3. Invoke only on desktop builtin profiles in existing refinement, before
   attached prop and bounds publication. Keep animal/Vore rollback semantics,
   tracked motion and custom humanoid calibration unchanged.
4. Compare the copied formulas and call order against the actual reference;
   get a bounded local Astra source review. Record its disposition.

The vertical slice is one desktop Ogre/Shambler using the same frame palette
for its support hand and equipped prop; no per-eye retargeting or new offsets.

## End-of-implementation acceptance

No builds, fixtures, runtime or performance checks in this slice. At the final
Linux/ARM qualification stage, cover both builtin profiles, nontrivial rigid
body rotations, source bind wrist calibration, reachable/unreachable grips,
degenerate or missing joints, independent arm rollback, unchanged dominant
wrist, tracked bypass and calibrated custom-avatar regression. Relevant live
appearance/headset/performance checks stay with the user. Source registration
or compilation alone cannot prove avatar behavior or finish the migration.

## Source implementation checkpoint

The one-file adapter is implemented in `Quake/r_avatar.c`; source review is
pending. It copies the reference Shambler authored-bend solve and support
endpoint calibration. Each optional arm and support solve has rollback;
ordinary frame palette publication and attached-prop owners are unchanged.

The existing physical solver has one `require_reached` argument: desktop
support refuses overreach rather than accepting its shortened target, while
both existing tracked callers pass `false` and retain their prior behavior.
The copied Ogre analytic specialization refuses stretch upfront because the
reference labels any analytic stretch `CLAMPED` and its support helper only
accepts `REACHED`. A final original-endpoint distance check also refuses inner
reach shortening. This is a narrow correction to the reference's status-only
check: unsupported contact preserves the original palette rather than claiming
a reached grip. The profile pole, wrist basis and dominant prop frame remain.

Only source reads and `git diff --check` have been performed. No builds,
fixtures, software execution or hardware/performance qualification occurred.

## Bounded local Astra source disposition

The existing local requested-Astra/Max reviewer found a near-antipodal
transport mismatch: the reused rotation helper collapsed tiny nonzero cross
axes into an arbitrary antipode, while the reference Shambler transport keeps
every nonzero axis. Adopted with a narrow helper argument: the new desktop
Shambler transport requests reference continuity; existing tracked/subtree
callers retain their previous threshold. No separate solver is added.

The reviewer source-accepted the strict-reach change, explicit `false` tracked
callers, original endpoint check, byte-identical dominant wrist and optional
support rollback. The transport correction awaits the final source recheck.
Effective model/effort metadata was unavailable; this is bounded advisory
source evidence and no executed acceptance or certified model provenance.
