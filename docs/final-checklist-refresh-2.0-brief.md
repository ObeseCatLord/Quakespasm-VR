# Final checklist refresh: local senior review brief

2026-10-01. User requests a full enumeration of missing features through senior
review before further implementation. The previous complete source audit is the
base; this pass must challenge coverage and reconcile subsequent integrations,
not manufacture another migration or silently reduce the surviving scope.

## Verified environment and evidence

| Fact | Evidence and status |
| --- | --- |
| Writable checkout | Established 2.0 checkout at `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`. Main and all siblings read-only; no branch check/switch needed. |
| Committed baseline | [verified: git log] `99eea1f0`; canonical `docs/final-checklist-2.0.md` records 18/22 findings source-integrated, four open, and Q01 source-integrated. Counts do not certify behavior or give a completion percentage. |
| Existing full audit | [verified: read artifacts] `final-scope-enumeration-2.0-worksheet.csv` covers 185 rows; `final-scope-lead-source-review.md`, `final-scope-sidecar-source-review.md`, `final-scope-senior-disposition-2.0.md` and `final-scope-interface-history-2.0.md` contain original source dispositions and supplemental obligations. Frozen S/M/Q/X/R labels describe snapshot `2b420380`, not today's status. |
| Partial uncommitted implementation | [verified: git diff and source] C07 renderer/HUD changes in r_alias.c, glquake.h, gl_rmain.c and gl_screen.c; C14 extraction in gl_model.c. Workers instructed to stop at safe source point. Treat these as provisional, not integrated or accepted. Source ownership and tests remain paused during this review. |
| User-owned document | Dirty `docs/migration-2.0.md` is not approval/implementation evidence. Do not read it as current scope, edit, stage or revert it. |
| Tests | No final-tree builds, tests, executable probes or performance measurements. User requires testing only after implementation is complete. Source reads are allowed. Linux x86-64 and isolated native ARM client via ssh Foundry are final software gates; Windows builds and user live trials deferred. |
| Reference sources | Read-only primary `../quakespasm-openvr` pin51b452c018273647dcf94f4628a370267ff8fa91; QSS `../QSS-M` pin03a498aabc411e2e739adc815c5536b161b9626e; vkQuake pin4bc898f29073e8aa41069f0e79e3cb5a9eb73afa; Ironwail pin08d578136ff43d7d1ef38e636dfbfd3e844be7cd. Existing audit records XR pin and source inventories. Use pinned objects for reference differences. |
| Scope authority | Read `migration-scope-decisions.md`; all conversation constraints are represented there and in canonical checklist scope boundaries. Optional additions are mapped in `migration-useful-additions.md`, not blanket-approved implementation. |
| Review routing | [verified: main read only matching routing fields] Effective reviewer `gpt-6-astra`, effort `xhigh`. No raw session telemetry exported or modified. |

## Main verified current remaining list

1. **C02 — metadata publication:** initial full server/user information and slot
   retirement/reuse remain absent at native signon/spawn/drop publication owners.
   C03 ordinary lookup is integrated. `metadata-publication-2.0-review.md` records
   real parser/reliable size constraints and the unresolved stock-QSS-M recipient
   capacity policy. Do not infer a human answer or a negotiated capability.
2. **C07 — presentation smoothing:** native replay/ACK history and camera phase
   committed `3f8b398c`; coherent held draw/HUD/beam phase is uncommitted and needs
   main source review. [verified: source] `R_AliasDrawModelMatrix` exists only in
   working tree, while gameplay/shared matrix owner stays native. Do not call C07
   complete because a worker wrote code.
3. **C14 — QBJ3 equipment:** before-code three-phase plan committed. Optional
   model extraction is uncommitted; immutable attachment staging and raster,
   overlay, ShowTris and TLAS consumers remain required. [verified: source]
   gl_model.c now contains exact roots/digest but current r_vrik_render.c retains
   single attachment path. Reuse two model slots and existing draw/AS owners.
4. **C19 — portable Linux delivery:** native Meson/Nix/AppImage routes exist;
   portable dual-architecture artifact route is incomplete. Inputs researched,
   local Astra disposition committed, exact host/bundle policy and before-code
   slices still needed. Existing distribution directories do not prove closure.

[Verified: canonical commit receipts, not executable acceptance] the other18
C items and Q01 have committed source integrations and main review receipts.
[Unknown] final loaded-program, rendered, installed-artifact and peer behavior
until the consolidated qualification. Do not conflate that with missing code.

## Decisions for Astra and depth budget

Objective: validate whether these four exhaust actual remaining implementation,
or identify concrete additional missing behavior with source/reference evidence.
Check every185 ID against existing audit/current status, using actual consumers
for challenged claims and changed regions. Supplemental QC/interfaces/history,
preservation and optional features must retain explicit dispositions. Reuse the
verified earlier audit; do not redo all literal lookups or execute anything.

Lean: one canonical final checklist with separately labeled (a) missing/partial
implementation, (b) exact unresolved decisions, (c) consolidated software and
delivery gates, (d) exclusions/deferred/optional scope. Avoid reopening working
native graphics, movement, networking or resource owners solely to make review
easier. Existing owner plus narrow adapter is preferred over parallel layers.

Rejected: source-present equals accepted; untested equals unimplemented; old
frozen M means still missing; every optional candidate is required; preserving
all legacy names; new quad-view or skyroom scope; Windows/live hardware/perf
checks as current goal gates. Overlapping IDs can share a single checklist item.

Output <=2200 words: coverage statement, prioritized exhaustive remaining list
with ID/caller/file-line evidence, any newly discovered gaps, decisions versus
technical work, final qualification groups and scope corrections. List exact
unreviewed IDs if coverage cannot be completed rather than claiming exhaustive.
Recommend checklist wording/architecture simplifications, not production edits.
Read-only, no tests/builds/lint/probes/SSH/assets/telemetry/nested agents/commits.
Main will verify load-bearing claims, record dispositions and publish the final
enumeration. No additional implementation before this review is synthesized.
