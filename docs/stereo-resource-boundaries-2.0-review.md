# Native culling and optional-constructor senior dispositions

2026-10-02. Local read-only Astra/xhigh review follows the
[verified brief](stereo-resource-boundaries-2.0-review-brief.md). Main verifies
active model/effort scalars and independently checks the load-bearing findings.
No production renderer or framegraph change is justified by these results.
Qualification edits reuse the existing capture and constructor fixtures.

| Finding | Main disposition and verification |
| --- | --- |
| Existing same-target-eye comparisons provide a stronger exclusion control. | Adopt. Groups0/1 split the views; groups2/3 preserve the selected view but exclude the other model from both eyes. Main independently decodes all16 B/E/repeat comparison pairs: exact RGB equality. Require target FOV, target eye clip, center clip and recorded head flags invariant. Source-controlled head pose remains a recipe premise, not a separately recorded pose witness. Add these existing controls to the checker; no new GPU run. |
| Enabled rejected models need positive actual consumer observations. | Adopt. All supplied enabled identities have eight in-path cull and alias attempts, including rejected models. Require positive counts for every enabled identity. The adapter already asserts native return and zero/four accepted triangle deltas. End-frame queries remain diagnostic and are excluded from those counts. |
| Empty projected polygons are not direct pixel-absence measurements. | Adopt. Zero footprint output means projection is empty. Absence evidence comes from zero accepted native alias geometry plus same-target full-image controls. Positive cells additionally have1485unique native sampling footprints each with B/E influence and intended color. |
| Matrix arithmetic is conditional independence. | Adopt. Six-plane box/projection arithmetic is separate from native culling but uses recorded native matrices; it does not independently validate their construction. These configurations qualify either-eye survival and excluded-both rejection. The native four-plane union remains conservative for separated cones. GPU-bound uniform bytes are not sampled in this adapter; prior alpha uniform evidence does not transfer silently. |
| Cleanup clears owning arrays and secondary contexts, not all cached bindings. | Adopt. Main reads r_passes.c:713–740 and1058: cached physical binding handles survive destruction until reconstruction. No demonstrated production defect under synchronized rebuild. Replace nonzero-only context assertions with membership in the live admitted-pass ledger. Owning arrays and secondary-context render_pass fields are the explicit cleared scope. |
| Subsequent ordinary construction is not retained-state coordinator fallback. | Adopt. Rename the helper/result to state cleanup and prepared-input reset. Warp ownership is retired explicitly. Keep existing GL OFF/recovery evidence separately qualified; do not infer coordinator integration from fake Vulkan constructors. |
| Add a new observer/runtime/framegraph framework or repeat GPU capture. | Reject. Existing images already supply the requested controls; the adapter and actual native constructor fixture cover these bounded boundaries. No broad rewrite or duplicated state/policy. |

Astra recommends accepting both bounded boundaries after these assertion and
wording corrections. Final amended checker/component verification is recorded
in their result documents. Other frozen F05/F06 owners and final whole-goal F10
signoff remain distinct; this is not the final integration review.

Main final verification: amended32sample checker0 with16matching cross-controls;
both disposable missing-observation/changed-image controls reject with expected
messages. Amended constructor strict compile0/run0. Private main receipt:
FastGames/qsvr-stereo-review-final-q38i0meu/main-receipt.json. No new GPU capture.
Both bounded acceptance corrections are complete.
