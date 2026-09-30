# Tracked builtin avatar endpoint adapters

Date: 2026-09-30. Writable branch: `2.0`; solo project, bounded adapters.
Behavior reference: readonly `quakespasm-openvr` master
`51b452c018273647dcf94f4628a370267ff8fa91`.

Restore inherited tracked head/wrist/foot placement for Soldier, Enforcer,
Ogre, Knight, Death Knight, Shambler, Zombie and Vore. Keep native vkQuake
rendering and its single frame palette. Dog/Fiend and calibrated custom
humanoids retain their existing specialized paths.

## Evidence and environment

| Fact | Evidence/status |
|---|---|
| Generic retargeting transports bind-relative displacement, not absolute endpoints | [verified: destination `R_AvatarRetargetPaletteWithContext`] |
| Eight profiles have no tracked endpoint refinement | [verified: destination `R_AvatarRefineBuiltinPaletteWithContext`; bounded Astra source audit] |
| Reference translates tracked Head subtree and solves both wrists | [verified: reference `r_alias.c:4124–4163`] |
| Analytic arms accept stretch through 1.10, reject farther targets, preserve anatomical wrist basis | [verified: reference `R_VRIKRefineArmPosition`] |
| Tracked Shambler requires the direct Shoulder→Upper→Lower→Hand chain and bind segment lengths, with no analytic fallback | [verified: same reference dispatch and `R_VRIKRefineActualPathWithLengths`] |
| Foot goals use inverse-presented canonical foot origins, target wrist/foot orientation and sender confidence | [verified: reference `R_VRIKAvatarMapLowerTarget`; target renderer currently passes only usable bits] |
| Vore repairs final inward knees after supplied feet, including frames without supplied feet | [verified: reference `R_VRIKRepairAvatarInwardLegs`, `r_alias.c:4249`] |
| Native bounded leg math already matches the reference animated policy | [verified: destination/reference `R_VRIKSolveLeg` formula comparison] |
| Palette is refined once before attachment, bounds and GPU publication | [verified: `r_vrik_render.c:R_VRIKRenderPreparePalette`] |
| Numerical edge cases, appearance and build compatibility | [unverified: final qualification remains deferred] |

Destination: `quakespasm-2.0/Quake/r_avatar.c/.h`, `r_vrik.c/.h`,
`r_vrik_render.c`. Native skeleton view uses global bind matrices and resolved
semantic indices. Canonical Ranger lower input contains local angle orientation;
it must never be mistaken for the model-space target foot basis. Usability
continues to be present ∩ (tracked ∪ predicted), computed once by the renderer.

## Minimal adapter versus replacement

Parameterize the existing copied desktop analytic arm by side and tracked
policy; desktop still accepts only unextended reach. Add a bind-length option
to the existing physical solver only for tracked Shambler. Copy reference
mirrored pole construction/outward constraint into the native leg math owner,
adding a semantic-index initializer alongside its existing Ranger-name wrapper.

Expose one stateless model-space foot batch adapter: resolved hip/leg indices,
two usable bits, model-space goals, sender confidences and mirrored-pole policy.
It captures/restores each target foot basis and rolls back failing sides
independently. Return successful-side bits; no retained cache or wire state.
Capture paired poles before either side mutates the palette. Existing canonical
Ranger calls pass NULL pole/outward parameters and keep their call order.

Pass confidence through a new frame refinement entry point; keep existing
refinement wrappers with legacy confidence 1 for their supplied usable bits.
Dog/Fiend keep their existing lower solve policy. This is parameter transport,
not another sampling, interpolation, presence or protocol owner.

Rejected: replace retargeting with a second IK/animation system, use calibrated
custom-humanoid IK for generic monsters, import the legacy global rig cache,
or make per-eye work. These duplicate working ownership or change reach/bind
contracts without demonstrated need. No Vulkan/shader/descriptor changes.

## Ordered implementation and failure boundaries

1. Validate prepared floor-corrected context (or construct it for old callers).
   Translate Head subtree to inverse-presented canonical head. Then solve left
   and right arms independently. Preserve successes if another stage fails.
   Analytic tracked poles add authored up; Shambler uses the validated physical
   chain, authored lengths and bounded extension, without analytic fallback.
2. Add native resolved-index foot adapter by reusing bounded leg math. Preserve
   current animated/bind/fixed-axis fallbacks and no-stretch bounds. New mirrored
   policy copies paired seed construction, sagittal preservation and outward
   enforcement after final projection. Snapshot/restore optional failing sides;
   validate finite rigid output before publication. No second Hip solve.
3. Wire sender confidence and canonical foot origins through presentation inverse
   into the adapter for usable roles only. Existing retargeted foot basis remains
   authoritative. Keep custom humanoid and tracked animal dispatch unchanged.
4. Replace Vore's lateral-only shortcut with the same mirrored native adapter.
   Gather offending sides before mutation, goal=current foot, confidence=1;
   run after supplied feet on tracked and desktop frames, even without trackers.
5. Compare formulas/order with primary reference; bounded Astra source review,
   disposition and normal commits. Builds/tests/runtime probes remain deferred
   until full goal implementation ends; source review is not execution proof.

Expected production change: five files, roughly 300–450 adapted lines, bounded
stack snapshots, no heap allocation or new per-eye work. Reopen if it expands
into animation ownership or needs duplicate retained state.

## Review decisions

Current lean: one batch adapter (rather than exporting private leg rigs/three
helpers); old API wrappers remain; per-stage optional rollback; Shambler bind
lengths only; Vore uses identical final mirrored policy on desktop/tracked.
Overlap: supplied feet and inward-leg repair are the same native solve boundary,
not separate solvers. Challenge whether any proposed new abstraction can be
deleted. Review budget: these contracts and call order only, ≤900 words. Exclude
GPU renderer, importing/assets, networking, water, foveation and live testing.
Effective reviewer model metadata is not exposed by current delegation tools;
any requested-Astra/Max response is bounded advisory, not a certified skill pass.

Final qualification must cover all eight profiles, both sides, absent/partial
and predicted roles, zero/fractional confidence, reach limits/collapsed chains,
head descendants, authored wrist/foot basis, Vore no-tracker knees and prop/bounds
consumers, on Linux x86-64 then ARM. User owns live visual/performance testing.

## Bounded Astra advisory disposition

Requested local Astra/Max reviewer; effective settings cannot be verified by
the exposed tool. Source-only advisory returned 2026-09-30, not a certified
senior-skill pass. Main checked reference length selection, nonzero seed-axis
transport and the separate outward-axis/paired-seed fallbacks in source.

| Recommendation | Disposition |
|---|---|
| Bind lengths alone miss Shambler seed transport and collapsed-current-segment handling | Adopt: new tracked Shambler policy preserves nonzero transport axes and skips undefined current-segment rotations before solved branch translations. Existing physical callers remain unchanged. |
| Define foot adapter completion and partial failure | Adopt: private leg math reports completion; zero finite confidence is an accepted position no-op. Public committed bits permit bounded reach clamps, not exact requested arrival. Validate capacity, ancestry and separate branches; rollback each side. |
| Resolve both legs, save bases/seeds before any side mutation | Adopt; failure to construct paired seeds still permits animated pole with available outward axis. Invalid opposite rig must not suppress the valid side. |
| Confidence/presence/basis have different roles and bit layouts | Adopt: renderer copies confidence [left foot,right foot] without blending or using it as presence. Adapter restores orientation about solved origin, including children. |
| Head→left arm→right arm→feet→Vore, each snapshot refreshed | Adopt; renderer ignores optional result, so every failure must restore valid palette without discarding earlier successes. Floor correction is single-applied. |
| Desktop Vore replacement must not copy shortcut gates/exact residual rejection | Adopt: gather offending feet before mutation, use the shared mirrored solve and permit reference singularity clamps. |
| One implementation with thin old wrappers; no public rig cache | Adopt. Five-file production boundary retained. |

Model-foot API includes caller capacity in joints. Its side bits are 1<<0 left,
1<<1 right. A finite confidence ≤0 accepts an unchanged position/basis; invalid
confidence rejects that side. Output validation must check finite rigid matrices,
not merely a non-crashing solve. The bounded validation can remain local to the
CPU adapter rather than expose an animation/cache owner.
