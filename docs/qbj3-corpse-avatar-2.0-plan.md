# C13: independent inherited QBJ3 death/corpse avatars

2026-10-01. Before-code plan following local Astra xhigh review of
[the verified brief](qbj3-corpse-avatar-2.0-brief.md). C12 is integrated in
33d6b90a. No executable checks until all implementation is finished.

## Behavior and reusable reference

Copy the primary `R_VRIKQBJ3CorpseOwner` identity contract and death ordinal
classification. Known supported deaths are41–102 in the exact143-frame
`progs/player_qbj.mdl` model under QBJ3. Reserved player deaths retain their
slot's explicit selected alternate; queued corpses must be ordinary dynamic
entities beyond reserved players, with nondefault colormap equal to an occupied
scoreboard slot's translations. Both use current slot selection as the reference
does. Unresolved/default selections and unsupported/static/arbitrary entities
retain original mod art. Do not invent historical identity or new messages.

Each admitted corpse owns its canonical native animation input, retargeted
palette slice, prepared pointer-keyed record and BLAS. No death/corpse path may
sample live pose/lower targets or publish tracked root/muzzle. This includes
the native tracked-candidate fallback, not only the alternate path.

## Senior disposition

The reviewer used local Astra with effective gpt-6-astra/xhigh verified by main
from the matching turn-context metadata. The reviewer could not inspect its
own effective routing; main verified it independently. Source-only review,
with no tests or edits. Main spot-checked the load-bearing native BLAS prepass,
draw_done join, descriptor allocator and primary death/identity reference.

| Recommendation | Disposition |
| --- | --- |
| Gate death before every tracking/fallback path | Adopted: shared supported-frame eligibility broadens to deaths, but tracking and implicit native-package admission stay live-only. |
| Preserve exact dynamic/colormap/occupied-owner attribution | Adopted: copy reference predicate, revalidate selection and attribution before palette publication; entity and owner remain distinct. |
| One growable frame list, not another corpse publication owner | Adopted: combine staged state, candidate, palette scratch and per-slot prepared records in the current owner; existing aggregate storage/descriptors/consumers remain. |
| Reserve before filling; concrete CPU budget | Adopted: reserve for reserved players plus admitted corpse entries; cap CPU entries at8MiB and actual native entity bounds. Checked multiplication, failure preserves old usable capacity, overflow retains mod art. This is a resource budget, not a four-corpse assumption. |
| Optional gear must not hide valid corpse body | Adapted: retain ready native attached props if possible; clear incomplete prop state/bounds/muzzle on optional failure and keep completed death body. C14 will extend QBJ3 equipment, no new prop representation here. |
| Expand native BLAS prepass and remove redundant player preallocation | Adopted: source confirmed gl_mesh R_UpdateAnimatedBLASes already visits all entities/reconciles models. Lookup prepared dynamic entities there; remove duplicate r_brush reserved-player allocation loop. |
| Avoid extra desktop join for CPU growth | Adopted: draw_done completes previous preparation/draw CPU users before the next staging boundary; end handles submission, not CPU entry records. Keep first-model-load joins. GPU storage stays after matching-slot fence. |
| Do not promise general GPU-OOM fallback | Adopted: this slice guarantees native-art fallback for checked CPU capacity/admission failures. Native Vulkan allocation failures can remain fatal; no allocator rewrite. |

## Bounded implementation

1. Factor C12 admission into one shared selection builder. A main-thread
   StageAvatars entry point replaces the sole screen loop; count and reserve
   before filling, players first, then eligible dynamic corpses in entity order.
   Rebind embedded rig live/profile pointers after output copies. A needed model
   load can reset admission caches, so do not erase already staged entries or
   rely on an old source/target generation during a build.
2. Extend the existing CPU frame list. Keep one aggregate actual-joint count and
   buffer upload with existing maxStorageBufferRange / integer / alignment checks.
   Compact per-slot published records independently of staged entries, so failed
   candidates and holes do not truncate valid later corpses. Clear publication
   and counts on invalidation, clear current-slot records on preparation, free
   owned CPU storage at renderer shutdown. Do not infer GPU slot during staging.
3. Revalidate supported frames, owner selection and original model at candidate
   preparation. Death is ordinary animation and never native tracked fallback.
   Clear all incomplete optional prop fields and muzzle on death attachment
   failure. Keep the valid body palette and animation-derived conservative bound.
4. Reuse shared original-model eligibility in raster/BLAS. Extend only the native
   dynamic prepared-lookup condition in its existing BLAS prepass, remove the
   redundant player pass, retain static/native exclusions and model reconciliation.

Write set: Quake/r_vrik_render.c/h, Quake/gl_screen.c, Quake/gl_mesh.c,
Quake/r_brush.c. About180–280 changed lines expected; report/reopen if materially
larger or another owner is introduced. No avatar assets, network/QC state,
animation/rig algorithms or other checkouts may change.

## Consolidated acceptance

At final Linux/ARM qualification, show actual supported player-slot deaths and
multiple queued corpses with distinct entity transforms/frames/palettes,
animation bounds and shadows. Live pose/FBT/root/muzzle must not appear on any
death/corpse. Native/default/unresolved descriptors, vacant owners, slot reuse,
unsupported model/frame transitions and static entities retain reference art.
Missing optional gear keeps the selected body; malformed/failed admission and
CPU capacity limits publish no partial alternate. Growth/reset/teardown and
admission loss must not leave stale palettes or alternate casters. No runtime
or performance claims are made by source integration.
