# F05 valid-pose liquid categories senior brief

2026-10-01. Solo project, frozen F05. Local Astra/xhigh read-only, <=650words.
No full inventory, new rendering architecture, builds, edits or nested agents.
Main owns execution/source integration. Current production through bbfaf741;
allocation-only4a816461 changes no production. Correct checkout already2.0;
do not check/switch branch; main/reference/user migration doc untouched.

| Environment fact | Verification and limit |
| --- | --- |
| Current native GPU | [verified: executed] Assertion-enabled SDL3 native client, RTX4090/isolated validation1.4.357, stock e1m1, native tasks, MSAA4/SSAO1 by private config, r_oit0. Private Monado simulated HMD, actual runtime and native GPU images, not headset acceptance. |
| Actual counterexample | [verified: debugger/native queries] Controlled rigid90degree head/eye pose after actual location, matching submitted XR poses; normal setpos836832-322. Native Mod_PointInLeaf reads water(-3)/empty(-1), origins z -296.8139/-295.1341. Head valid1/tracked0 is untouched runtime input. tracking_basis_valid0, categories_valid0, mask0, exceptional0. Native client normal exit; no output pass asserted. |
| Source cause | [verified: main read] R_PrepareStereoFrame sets stereo_tracking_basis_valid only for head valid AND tracked and finite basis, for canonical tracked presentation consumers. Its later liquid-category block at gl_rmain.c:1011 also requires that flag, despite actual rendered eye origins and valid predicted location. Thus it bypasses categories/eye masks and falls back to center contents in R_DrawAlphaEntitiesTask. |
| Reusable owner | [verified: source] Native category lookup, wet-eye mask, separate alpha descriptor selectors, two-stage R_DrawStereoAlphaListAtStage already exist. Canonical R_TrackedPoseToWorld/R_TrackedPresentationOrigin independently require head tracked as well as basis validity. No new category state/renderer/list needed. |
| Deferred claims | [unverified] The current counterexample does not inspect opposite wet/dry color or authored translucent objects, forced one-eye visible geometry, KHR protected output or physical tracking loss. These remain existing F05/F06 qualifications. |

Decision lean: remove ONLY the canonical tracking-basis gate from liquid category
selection, replacing it with direct valid finite renderer-head/eye eligibility
if required. Native rendered eye coordinates should govern water order even when
pose is valid/predicted but not actively tracked. Preserve canonical avatar/wire
tracking gates and native opaque multiview. Bound production delta to this one
condition; compare the minimal adapter with a separate per-eye render pipeline
(rejected: existing selectors already implement this exception).

Verify source/flow before critique; challenge whether this is necessary, whether
the existing rendering eligibility already proves a usable head/eye origin, and
which minimal direct guards avoid accepting fabricated/stale invalid coordinates.
Prioritize correctness over fixture convenience. No broad relaxation of tracking
policy. Recommend the smallest native output/oracle needed for this specific
failure. No human preference blocks this source-derived decision.

Read gl_rmain.c R_PrepareStereoFrame, R_TrackedPoseToWorld,
R_TrackedPresentationOrigin, R_DrawAlphaEntitiesTask/R_DrawStereoAlphaListAtStage
and backend locate_frame/VRXR_StereoClip as needed. Current private
/tmp/qsvr-final-qualification-thchgzi8/stereo-boundaries-current:
probe-boundary.gdb/run-boundary.log/diagnostic-boundary.json. Prior two desktop
startup attempts lacked -openxr and are retained as recipe failures, not XR
failures. First actual XR diagnostic chose a solid BSP sliver; second corrected
boundary/reference reset yields the real opposite category case. No masks,
draw lists, capabilities, graphics output or head tracking flags were assigned.

## Final source decision

Local senior effective gpt-6-astra/xhigh was independently checked. Main read
the cited normal frame-begin, location/projection and canonical guard sources.
The brief used two inaccurate canonical symbol names; actual guards are
R_TrackedPoseBasis and R_TrackedHeadBodyOffset. Their stricter behavior stays.

| Recommendation | Main disposition |
| --- | --- |
| Delete only category tracking-basis conjunct. | Adopted before code: one-condition gl_rmain.c delta. Successful frame location/projection already validates usable poses, and existing category checks retain finite computed eye origins. No duplicated head validity policy or avatar/network relaxation. |
| Require authored alpha overlap, not wet tint alone. | Adopted acceptance distinction. Current native controlled pose proves category selection failure only. Next native run will check mask1/mask2/selectors and inspected two-eye images; absent actual translucent geometry cannot establish composition. Full alpha output stays open until that premise is present. |
| Check state retirement when preparation ineligible. | Adopted within existing owner; reset at every preparation must remain, not a new lifecycle. |
| Correct canonical symbol names / controlled-input scope. | Adopted. Real location flags remain untouched; no physical tracking-loss claim. |

Main applies the exact mechanical deletion, affected native host rebuild and
native mask/selector output run. Any broader alpha proof uses existing entity/
asset/render owners, not a synthetic parallel renderer. This decision is not
whole F05/F10 completion or a physical headset/performance gate.
