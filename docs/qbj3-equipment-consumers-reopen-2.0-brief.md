# C14 consumer estimate/reuse reopening

2026-10-01. Before further edits. Local senior review of the paused259-line
r_alias.c/r_brush.c patch: original phase3 reassess180 and combined800 bounds
are exceeded (217 extraction +398 producer +259 consumers =874). No builds,
tests, compilers, probes or game/asset execution. Source integrated producer
3b80ac1a; consumers uncommitted/unaccepted. Main read the whole current diff.

## Verified environment and findings

[verified: main full diff] Writable established quakespasm-2.0 only, pinned
primary ../quakespasm-openvr51b452c0 readonly. Current r_vrik_render.h fixed two
records (geometry, affine, bound, validity/count); ordinary Ranger one, QBJ pair
zero or two. Same model/frame/BLAS owners. Main owns docs/other packaging;
independent Luna owns only Packaging/Linux/package.py/host-policy.json.
User-owned docs/migration-2.0.md untouched. Review readonly/no nested agents.

[verified: main diff] New R_AliasUsableAttachments wraps the native surface
chain checker in an outer record loop and reindents its entire body. This accounts
for much of apparent growth without new behavior. Main lean: retain native
single-record checker nearly verbatim, parameterized by r_vrik_attachment_t,
then a small pair wrapper checks each record before returning a complete count.
This preserves reuse and per-pair material/finite admission at existing boundary.

[verified: main diff] R_DrawAliasModel declares attachment_geometry twice in
the same scope (around1207/1234), a compile error visible without execution.
Matrix/shade preflight uses checked_attachment_count plus attachments_valid and
resets count after loops; overlay and ShowTris repeat similar boolean state.
Lean: one local count, set0 and break on preflight failure, no draws until all
requested records passed; no new persistent consumer state.

[verified: main diff] Overlay preflights all optional matrices/inflation, issues
body/allprop masks before outlines/fills. ShowTris adds two static identity views
with zero poses. Main draw preserves C07/native draw matrices, ordinary body,
original-entity dispatch/winding and independently transformed prop lighting.
Target presentation never enters prop entity matrix. Confirm actual draw
readiness/nulltexture behavior and native zero-pose/material ownership.

[verified: main diff] TLAS count and emission share one bounded helper. It builds
checked local arrays then copies them to caller arrays, but caller count0 already
discards outputs on failure. Main lean: write caller-owned local arrays directly,
return fullcount only after all pass; caller emits nothing on0. No persistent
partial frame publication. Both passes use same immutable prepared record and
original native geometry/AS owner. Judge whether this simplification is safe;
do not discard identity/readiness/finite checks merely to fit line estimate.

## Decision and review scope

Retain existing incremental design if source confirms it. Alternatives: reduce
incidental churn/duplicate local state using existing per-record helpers; or
export shared rendering/AS attachment validation API. Prefer first: no demonstrated
need to couple adjacent Vulkan owners or create another attachment renderer.
Reject broad rewrite/new cache/shader/upload layer, dropping ShowTris/pair atomicity
or preserving temporary old-field duplicates for ease of implementation.

Verify then critique actual sources, including producer/reference only where
load-bearing. Rank real defects/reuse opportunities; recommend minimal fixes and
justified phase3/combined bound. Is original180 still achievable after simplification,
or did we underestimate necessary multiple-consumer adaptation? No enterprise
ceremony/feature audit repeat. Output<=1200words with exact file-line evidence,
prioritized recommendations, any genuinely human decision, final qualification
limits. Main spot-checks and commits disposition/plan before refinement.
