# C13 QBJ3 corpse avatar: verified design brief

2026-10-01; local Astra xhigh requested. Solo project, bounded adapter only.
No tests/builds/probes are authorized before all implementation is finished.
C12 live-player admission is being implemented separately under its committed
plan. This brief concerns known death frames and independently attributable
queued corpses, not new avatar formats, networking or physical melee.

## Evidence and environment

| Fact | Evidence / status |
| --- | --- |
| Writable 2.0 checkout | `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`; only this checkout may change. No branch switching/checking, no changes to user-dirty docs/migration-2.0.md. |
| Primary reference | Readonly sibling quakespasm-openvr, pinned 51b452c018273647dcf94f4628a370267ff8fa91; `Quake/r_alias.c` 5732–5980. |
| Supported QBJ3 contract | [verified source] primary strict `player_qbj.mdl`, 143 frames, ordinal0–142; death41–102. |
| Corpse attribution | [verified source] primary `R_VRIKQBJ3CorpseOwner`: only normal dynamic entities beyond reserved player slots; supported death model/frame; nondefault colormap exactly matches an occupied scoreboard slot's translations. Static entities/arbitrary aliases are excluded. |
| Selection | [verified source] only explicit locally resolved alternates survive death/corpse; native Ranger or unresolved selection retains mod art. Current owner's selection is reference behavior, not captured historical identity. |
| Tracking | [verified source] primary sets `live=false` for death/corpse and never samples prior pose/lower targets. Scratch corpse skin state is reset per substitution; corpse never borrows the living owner's palette. |
| Current 2.0 owners | [verified source] r_vrik_render.c contains staged selections, candidates, per-frame aggregate storage allocation and double-buffered palette records, currently arrays limited to MAX_SCOREBOARD. PrepareFrame loops reserved player entities only. Native raster/BLAS/TLAS already consume immutable records keyed by entity pointer. |
| Shared animation reference | [verified source] AlternateCandidate copies the original entity, swaps canonical model, uses that entity's native frame/interpolation state and retargets its own baked palette. No original entity mutation. |
| Current main admission | [verified source] gl_screen.c2630 main-thread loop stages reserved players before new render tasks. Needed first model load joins previous CPU end task. |
| Frame ownership | [verified source] SCR_UpdateScreen waits draw_done before returning; it leaves only end submission asynchronous. VR GL_BeginRendering joins previous end. Palette prepare depends on begin and store_efrags; render/BLAS consumers depend on palette prepare. Prepare clears publication until complete. |
| AS coverage | [verified source] r_brush.c3122 allocates missing palette entity BLAS only for reserved players, then native TLAS scans all dynamic/static entities. Corpse coverage requires expanding the palette-specific allocation loop, not a second AS owner. |
| Allocator | [verified source] Mem_Realloc in mem.c120 is ordinary realloc/SDL/mimalloc; can fail. Existing reference arrays cannot hold an arbitrary mod body queue. |
| C14 relationship | [unknown implementation] optional QBJ3 equipment is separate. Do not redesign the prop representation in this review; ensure corpse admission doesn't require optional equipment to keep body visible. |

## Lean: extend the existing frame list, not clone player state

Use one main-thread `StageAvatars` entry point replacing the sole screen staging
loop. Reuse the existing admission builder for both players and attributed
corpses; keep original entity and resolved owner selection on each entry. Every
eligible queued corpse receives its own candidate, native animation input,
palette slice, prepared entity-pointer record and AS data. Mark corpse/death
entries ordinary/untracked before any pose/lower/root/muzzle sampling.

Current lean is a bounded dynamic staged/candidate/prepared list, retaining the
current single storage upload and per-slot descriptors. Capacity depends on
reserved players plus eligible attributed corpses, not every map entity's full
256-joint palette allocation. Reuse native Mem allocation and shutdown/reset.
Check size multiplication, failure and device storage range; preserve original
mod art on admission/resource failure. Refresh self-referential rig live/profile
pointers after any staged relocation. Do not preserve a selected corpse's pose
history in another protocol/cache or give it its owner's entity pointer.

Alternative: retain static player arrays and add a separate bounded corpse
vector using the same builder. This may simplify staging, but risks duplicating
publication/capacity and animation policy. Reviewer may prefer it if it reduces
actual complexity while retaining a single palette/frame consumer.

Rejected: huge MAX_EDICTS palette arrays, hardcoded four-corpse cap without mod
evidence, per-corpse GPU submission/descriptors, live-owner palette reuse, CPU
vertex skin cache rewrite, a new avatar identity generation protocol. Existing
network and model owners remain.

## Open decisions and review contract

1. Pick the smaller existing-owner extension: one staged vector versus static
   players plus dynamically staged corpse records. Both must use one policy
   builder, native per-entity animation and existing aggregate upload.
2. Determine whether any growth requires a prior end-task join or whether the
   verified draw_done boundary plus matching frame-slot wait is sufficient.
   Do not serialize every desktop frame merely for allocation convenience.
3. Verify loss/fallback semantics: unresolved descriptors, stale owner/slot,
   transition to unsupported model, resource exhaustion, optional gear failure.
4. Corpse-specific BLAS allocation should cover prepared dynamic entities,
   preserving native static entities and renderer eligibility together.

Expected adapter: r_vrik_render.c/h, gl_screen.c, r_brush.c, with already-shared
C12 raster/BLAS model eligibility. Approximately 180–280 changed lines. Reopen
if it duplicates another owner or materially grows. There may be overlap between
selection and per-entity staging; merge it rather than introducing two policies.

Verify the load-bearing sources before critique. Read only; other Luna workers
currently own C12 avatar files and C21 timing files. No edits or agents. Return
<=1000 words, prioritized blockers/recommendations with exact file/symbol
evidence, which option to adopt and reasons, and only genuinely human choices.
Do not repeat the full scope audit, animation/rig algorithms, OpenXR, audio,
packaging, foveation, Windows or runtime/performance testing. Final software
acceptance will show supported deaths and multiple body queues, independent
palette/shadows/culling, no tracking/root/muzzle on corpses, and original art for
unsupported/unresolved paths; helper counters alone cannot close C13.
