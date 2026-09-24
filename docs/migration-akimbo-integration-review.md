# QBJ3 client integration: Astra senior review disposition

This review audited the first QBJ3 twin nailgun vertical after it compiled and
its installed QC shot fixture passed. The fixture injects poses directly, so it
does not prove the client render/muzzle transforms. Astra Max reviewed the
adapter and found three release-blocking client errors. The narrow vkQuake alias
renderer adapter remains the selected architecture.

| Finding | Disposition |
| --- | --- |
| Left split mesh used `origin_y - held_offset_y`. | **Fixed.** The alias matrix now uses the donor's `2 * held_scale * origin_y - (origin_y + held_offset_y)` before global scaling, without mirroring the split mesh. For the installed QBJ3 source/calibration this is `-7.67895` instead of `-46.92895`. |
| Muzzle contact was passed to the matrix as a decoded position. | **Fixed.** Convert each source contact to raw MDL vertex coordinates with `(anchor - source_origin) / source_scale`, then transform through the same alias matrix used for rendering. Installed coordinates resolve to left `(244,181,217)` and right `(244,73,217)`. |
| A published pair could survive XR teardown and render on desktop. | **Fixed.** Clear the pair on the main owner when rendering fails or switches to desktop; pair entity access also requires active stereo. Desktop continues through vkQuake's original viewmodel path. |
| Generated and custom halves could mix, or an unverified override could use source anchors. | **Fixed for this vertical.** Main-thread preparation admits only two generated halves; custom overrides fall back to the original viewmodel. The loader owns this provenance check; the per-frame readiness path does no filesystem probe. |
| Pair commands depended on the ordinary enhanced-model muzzle lookup. | **Fixed.** The complete pair is prepared first. Its dominant physical muzzle supplies the base private pose; ordinary muzzle calibration runs only when the pair cannot be admitted. |
| Exact sample matching might starve the command producer. | **Rejected as a problem.** `Host_Frame` consumes and sends the previously rendered XR sample before `SCR_UpdateScreen` acquires the next sample. Keep the exact sample check. |
| Replace vkQuake rendering or add locks around pair entities. | **Rejected.** `draw_done_task` is joined before the next host tick, and model loading happens on the main owner before draw tasks. |

The installed MDL header independently confirms scale/origin and raw contacts.
The corrected implementation still needs software proof that a prepared client
pair serializes poses from those contacts through a pinned command, plus the
user's later headset alignment check. The existing installed-QC fixture proves
server shot semantics and fallback, not those client transforms. Desktop
appearance and controls remain vkQuake's defaults except for the separately
ported shared features.
