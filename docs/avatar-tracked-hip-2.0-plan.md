# Tracked animal Hip position adapter

Date: 2026-09-30; writable `2.0` only. Preserve inherited Dog/Fiend FBT Hip
position/rotation through the existing CPU palette and cached presentation.

Verified behavior reference: readonly primary master `51b452c0`,
`r_alias.c:R_VRIKRefineAvatarPaletteImpl` maps the canonical solved Hip origin
through the presentation inverse, forwards sender Hip confidence, and applies
`r_vrik.c:R_VRIKApplyLowerBodyWithPolePolicy` before restoring mapped branches.
The positional correction is bounded to 96 model units then multiplied by
confidence clamped to [0,1]. Hip orientation remains independent of confidence.
Mapped semantic branches keep their already-retargeted pose; unmapped Hip
children follow the corrected Hip.

Verified destination: `R_AvatarApplyTrackedAnimalHip` restores only rotation.
Generic retarget origins use bind-relative displacement and do not subtract the
prepared inverse's floor translation. Consequently a supplied Hip remains at
the uncorrected origin when floor correction is nonzero. The renderer samples
sender confidence already, but the frame adapter forwards only two foot values.

Minimal adapter: copy the reference bounded positional correction into the
existing animal Hip helper, before its current mapped-branch restoration.
Extend the newly introduced frame confidence array to [left foot,right foot,Hip],
computed at the same renderer boundary; old refinement wrappers retain default
confidence 1. Dog/Fiend physical feet keep their existing reference policy,
which does not blend confidence in the successful physical-path branch.

No new Hip solver, wire field, interpolation/presence owner, retained rig cache,
renderer or per-eye work. Native Ranger/custom-humanoid dispatch and generic
monster feet retain their policies. Expected scope: `r_avatar.c/.h` and
`r_vrik_render.c`, approximately 30 adapted lines plus parameter names.
Reopen the plan if a second transform/sampling owner becomes necessary.

Implementation: validate confidence and mapped target; copy position bound and
blend; preserve rotation at zero confidence; update frame parameter transport;
retain whole optional Hip rollback and independent upper/lower stages. Main
source-compares formulas with primary, then obtains bounded local Astra source
review. Build/tests/runtime probes remain deferred until full implementation.

Final Linux/ARM qualification must cover both animal profiles, floor correction
zero/nonzero, confidence zero/fractional/one/invalid, mapped versus unmapped Hip
children, no supplied Hip, unrelated generic/custom avatars and independent
failure rollback. Shipped asset appearance and numerical behavior are unverified;
source mapping alone does not establish full FBT or migration parity.

## Implementation and source disposition

`b64fbe74` implements the three-file adapter. The bounded local Astra advisory
accepted the actual source with no actionable residual defect in its scope.
Main verified reference order: inverse-presented canonical goal, 96-unit delta
bound, confidence blend, independent orientation and mapped-branch restoration.
The renderer explicitly maps the named Hip wire role (index 0) to adapter slot 2;
foot slots remain 0/1. Legacy wrappers use NULL/default confidence 1.

| Review recommendation | Disposition |
|---|---|
| Retain cached floor translation single-applied | Confirmed; prepared context reused, fallback built only if absent. |
| Keep zero positional confidence independent from Hip rotation | Confirmed; zero leaves position unchanged, rotation still follows tracked source. Invalid confidence/distance restores optional Hip stage. |
| Restore mapped semantic branches after applying the Hip transform | Confirmed; unmapped children follow Hip, mapped nonvirtual branches retain saved pose. |
| Preserve independent animal upper/feet and generic/custom dispatch | Confirmed in bounded callsites; full FBT/asset parity remains unqualified. |

`git diff --check` passed. No build/tests/compiler/runtime/fixture execution.
Effective reviewer metadata is unavailable; requested-Astra/Max advisory is not
a certified senior-skill pass. Linux/ARM and shipped-asset qualification remain
deferred, and the full migration goal remains active.
