# C14 consumer reopening: senior disposition

2026-10-01. Local gpt-6-astra/xhigh, effective routing independently verified by
main. Review input [verified brief](qbj3-equipment-consumers-reopen-2.0-brief.md)
af365e7d; paused consumer patch259 changed lines, unaccepted. Main read the full
diff and independently checked the load-bearing source findings below. No tests,
builds, compilers, probes, game/asset execution or telemetry exports.

Keep the existing fixed records and native consumer/resource owners. Estimated
growth combines necessary four-path adaptation with avoidable reindentation and
local-state duplication. No evidence warrants another attachment renderer or
shared public raster/AS validator. No new product choice/feature was established.

| Recommendation | Main disposition / independent source check |
| --- | --- |
| Remove duplicate attachment_geometry declaration | Adopted. Main full diff confirms both declarations in R_DrawAliasModel's same scope. Keep one local fixed array, no substitute alias/state. |
| Match the actual native rolling skin fallback | Adopted. Main read R_DrawAliasSurfaces:1140–1169: once an out-of-range surface resets its local skinnum to0, later surfaces use0. Private checker must mirror that within each prop chain, not independently clamp the original requested skin per surface. Reset the requested skin separately for each prop call. Do not change native global draw policy. Exact QBJ assets triggering this discrepancy are unverified. |
| Preserve per-record checker and add small pair wrapper | Adopted. Main diff shows an outer record loop reindents native checks. Parameterize the native checker by one attachment; retain chain/resource/finite checks, then check all counted records through a bounded wrapper. Complete count only after all succeed. Producer identity/CPU-GPU/bounds and AS readiness remain at their existing owners, not a new public API. |
| Simplify local preflight states | Adopted. One local count can become0 with immediate break on matrix/shade/inflation failure; no optional draw occurs until complete preflight. Main checked current masks precede all outlines and ordinary shade is copied before body presentation transforms. Preserve each prop's independent transformation of that original shade direction and winding. Hoist ShowTris's zero-pose lerpdata. |
| Keep static palette/material fallback ownership | Adopted. Main read MD5_UploadAvatarPropViews:5878: identity joint, borrowed textures, owned buffers. Static props use zero poses and no tracked-palette override. Native missing tx uses greytexture; overlays/ShowTris use persistent nulltexture. No global texture/device recovery layer is justified here. |
| Remove TLAS scratch-to-scratch copies | Adopted. Main checked count/emission callers only consume arrays below the returned count. Write their temporary arrays directly; failure returns0, success fullcount. Retain prepared/body identity, native body-build readiness, GLMesh_AvatarPropBLASAddress (which delegates readiness at gl_mesh.c:1297) and finite final matrices. |
| Count pass is a capacity upper bound | Adapted wording. Main read r_brush.c:3208 body matrix/presentation can reject before emission; fewer emitted instances is safe. Required invariants are no capacity overrun and no partial attachment pair. Same immutable prepared records and shared helper suffice; do not add persistent cached counts. |
| Reopen phase3/combined estimate | Adopted. Expected190–220 changed lines; stop/reassess above240 or new ownership/duplicated policy. Prior217+398 means expected805–835 combined, threshold855. This supersedes180/800; estimates do not accept the current259-line patch. Preserve necessary guards, avoid distorting code merely for line count. |

After committing this disposition and revised plan, a bounded Luna refinement
may edit only r_alias.c/r_brush.c. Main source review and integration remain
required. Final rendered/software qualification must exercise both attachments,
all consumer paths and one-prop failure yielding neither optional prop while
retaining selected body, ordinary Ranger and independent C13 corpse behavior.
