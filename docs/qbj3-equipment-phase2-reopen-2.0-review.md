# C14 phase2 reopening: senior disposition

2026-10-01. Local gpt-6-astra/xhigh reviewed the paused producer patch against
[the committed verified brief](qbj3-equipment-phase2-reopen-2.0-brief.md),
1169c1a0. Main independently verified effective routing, then checked the
load-bearing findings against current source. Review and checks were read-only;
no builds, tests, compiler, executable probes or game runs occurred.

The two-record design remains the minimal adaptation at existing model, frame,
raster and BLAS owners. The paused 392-line producer patch is not accepted or
compile-ready: consumers still use the removed single-prop fields. Keep those
edits paused until this disposition and revised before-code plan are committed.

| Finding / recommendation | Main disposition and evidence |
| --- | --- |
| P1: custom cache uses the wrong index | Adopted. Main checked r_vrik_render.c:271–294: native custom cache index is id-PLAYER_AVATAR_COUNT, with 64 entries declared at141–142. New EquipmentCurrent at1114 indexes with the absolute avatar ID. Validate the ID before subtraction, then use the relative index for attempted/model identity checks; retain descriptor/model/skeleton/view identity validation. |
| P2: optional source staging loads unused equipment | Adopted. StageEquipment at625 currently performs optional lookup/loading before checking the selected target policy. Gate by QBJ3 and effective selection->target_rig.profile ATTACH_HAND before lookup. Use the post-normalization target profile; source extraction and source presentation remain independent of that policy. |
| Reuse native affine composition | Adopted with finite failure policy. Main checked mathlib.c:417: R_ConcatTransforms supplies the same 3x4 operation, with non-const inputs and a distinct output requirement. Replace the duplicate general multiplier with a small wrapper using local copies, distinct output and finite validation. Float overflow conservatively refuses the optional pair and retains the body. No new public affine API or retained double implementation without demonstrated need. |
| Preserve source anchor semantics | Adopted. Rotation-only basis and full-forward origin mapping remain necessary before the rigid socket helper; compose socket * source.forward * root.bind afterwards. Validate the final overwritten origin as well as the basis. Root-local extracted vertices must not receive target body scale twice. |
| Pair atomicity extends through consumers | Adopted. Phase3 must preflight both records, material, matrix, lighting and required BLAS before any optional draw/emission. Raster, co-op masks/outlines, ShowTris and TLAS count/emission share bounded pair admission. A consumer rejection retains the independently valid body. Use local fixed arrays, not another persistent publication owner. |
| Existing staged/frame/resource lifetimes suffice | Adopted. Keep borrowed model/skeleton/views and copied matrices/context at existing owners; no pointer to the local resolved rig. Retain native compaction profile rebinding, conditional AS readiness, finite bounds and maximum body/prop union. No new cache or per-frame mesh stream. |
| Remove incidental duplication and preserve native comments | Adopted. Replace comma-expression fallback with explicit control flow. Pair construction publishes only after both succeed, so redundant second clearing is unnecessary. AlternateCandidate already clears muzzle on entry. Restore native Ranger comments and unrelated formatting; clarify ValidatePropView's caller-dependent body-failure policy. |
| Reopen the estimate rather than drop necessary guards | Adopted. Phase2 expected350–380 changed lines; stop and reopen above400. Phase3 expected140–180, reassess above180. With phase1's217, combined expected707–777; reopen above800 or on new ownership/policy duplication. This supersedes the earlier650 bound, which is no longer credible. |

No new product question or independent feature was identified. These corrections
remain within final-checklist C14. Do not claim rendered acceptance from source
review. Final qualification must cover both anchors, left dominance, skin/glow,
either-eye culling, overlays/ShowTris, shadows, source misses and lifecycle;
ordinary Ranger and independent C13 corpse presentation must remain valid.
