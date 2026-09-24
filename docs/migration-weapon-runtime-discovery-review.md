# Runtime weapon discovery on the vkQuake 2.0 wheel

## Goal and scale

Port inherited runtime viewmodel discovery for mods with incomplete or absent
wheel/schema files. Keep one vkQuake client-stat and wheel-catalog owner. This is
a solo-maintained engine, so the solution should be a narrow adapter rather than
a second dynamic weapon registry or parser. The product reference is pinned
QuakeSpasm OpenVR `Quake/vr.c` in `migration-feature-map.md`.

## Checked facts

| Claim | Evidence |
| --- | --- |
| The source observes `STAT_ACTIVEWEAPON` and `STAT_WEAPON` once per connected frame from `VR_UpdateScreenContent`; only weapon-looking precached model paths qualify. | [verified: pinned source `Quake/vr.c:10030-10032,11494-11516,1274-1308`] |
| Source observations never invent an impulse; unknown entries can be visible but not selectable. Known schema slots can learn a viewmodel path, and model-index observations reset across maps. | [verified: pinned source `Quake/vr.c:11516-11575,11583-11594,11790-11810`] |
| Source probes explicit private ownership stats before trusting an unknown weapon's ownership. Arbitrary `STAT_ITEMS` bits are not trusted for discovered entries. | [verified: pinned source `Quake/vr.c:1200-1233,1735-1764`] |
| Target already has a pure observation helper, model-path g_/v_ equivalence and visibility precedence, but does not call `VR_WeaponCatalog_Observe`. | [verified: `Quake/vr_weapon_catalog.h:20-128`; repository `rg` call-site search] |
| Target already owns roster/schema/profile entries and loads them on game switch/start. `VR_WeaponMenu_PrepareModels` resolves paths before draw tasks, while `SCR_UpdateScreen` is a main-thread boundary after `GL_BeginRendering`. | [verified: `Quake/vr_weapon_menu.c:173-202,897-946,1241-1293`; `Quake/common.c:3370-3373`; `Quake/sv_main.c:2310-2313`; `Quake/gl_screen.c:2470-2525`] |
| Target `VR_WeaponMenu_EntryOwned` currently falls back to `STAT_ITEMS` for any selector when no explicit owned stat is set. That would falsely expose discovered entries with reused item bits. | [verified: `Quake/vr_weapon_menu.c:1034-1073`] |
| Target scene/UI workers read a copied wheel frame. Catalog and model cache mutation while they record would race or leave stale assets. | [verified: `Quake/vr_weapon_menu.c:1208-1293,1680-1732`; `Quake/gl_screen.c:2513-2535`; `docs/migration-wheel-3d-design.md`] |

## Incremental design lean and alternatives

1. **Adapt the existing catalog.** At the successful main-thread render begin,
   while signed on, inspect the current active selector and precached alias
   viewmodel. Match existing entries by explicit selector and g_/v_ path
   equivalence; there is no need for a second observation-history table.
   learn a path only for schema/roster slots without an authored model, and
   invalidate that slot's resident model cache when its path changes. For an
   unmatched model, append a `DISCOVERED` entry to the existing mutable catalog
   with a copied path, exact active selector, no impulse, and active-only
   ownership. Copy stock entries into that catalog once if it was
   otherwise using the static stock catalog. Keep the user's schema/profile
   paths, impulses, ownership and order authoritative. Reset map-index
   observations on worldmodel change and all learned state on game reload.
2. **Keep a separate runtime catalog and merge on each draw.** Rejected lean:
   duplicates visibility, IDs and model-resource invalidation around the
   existing `CurrentCatalog`, `BuildVisible`, release and frame-copy paths.
3. **Observe in the packet/stat parser.** Rejected lean: `STAT_WEAPON` and
   `STAT_ACTIVEWEAPON` can arrive separately; the client may not have a final
   precache/name until signon. The render-begin setup owner provides a coherent
   read after parsing and before worker tasks.
4. **Infer a weapon impulse from selector or model name.** Rejected by source
   behavior and mod compatibility: QuakeC impulse namespaces are arbitrary.

Smallest end-to-end proof: equip an unknown mod weapon whose model path looks
like a weapon; its model is shown as an unselectable discovered slot, without
turning a reused stock bit into a guessed switch command. Equip a schema
weapon with no authored model; the known slot learns the current viewmodel
without changing its impulse. Change maps and reuse the model index for a
different asset; stale pointers/paths must not be selected. Desktop stock
game and OpenXR wheel continue sharing one catalog. Hardware testing is
deferred; a local Linux build and focused logic/runtime checks are the code
checkpoint.

## Questions for Astra

- Verify the source/target anchors and challenge the one-catalog design if it
  misses an existing owner or a task/frame lifetime.
- Identify the safest identity rule for matching a schema slot with no authored
  model and for keeping a discovered model distinct when a stock bit is reused.
- Determine whether map-index observations should be state at all, or whether
  path-only records suffice; identify the minimum invalidation needed for the
  existing resident asset cache and copied draw frame.
- Audit the proposed `DISCOVERED` visibility/ownership semantics and any
  hidden dependence on QSS-M private stats or Dwell mapping.
- Rank the main failure modes and identify deletion/simplification opportunities.

Do not re-review OpenXR session lifetime, renderer pipelines, foveation,
network transport, or the complete 185-item feature map. Do not edit code.
Return a prioritized critique and concrete recommended seam; separate true
human preference decisions from technical decisions we can make here.

## Astra senior-review disposition

Astra was requested as `gpt-6-astra` at `xhigh`; the local agent turn metadata
confirmed both effective settings. Main spot-checked the reset hooks, release
gate, model provenance and task join against the cited code. The review changed
the implementation plan in three substantive ways: it required an explicit
client reset hook, removed unnecessary observation history/private-stat
   inference, and identified a fallback release regression under selector reuse.

| Review recommendation | Main decision |
| --- | --- |
| Use a real client map/disconnect reset, not worldmodel-pointer detection. | **Adopt.** `CL_ClearState` is called by serverinfo including same-map restart, and `CL_Disconnect` has a separate path. Cancel the wheel and clear borrowed model/frame references before client model teardown; keep copied learned paths until game reload. |
| Track authored/profile/fallback model provenance and learn only into a unique compatible roster row. | **Adopt.** A model path's mere presence does not make it authored. Ambiguous selector matches remain unselectable discoveries. |
| Suppress false `active` state on a stock/profile fallback when the equipped selector matches but the valid observed viewmodel does not. | **Adopt.** Release currently refuses an active row's impulse. Preserve the fallback row as owned/selectable while requiring matching model identity to call it active. |
| Give unknown discoveries active-only ownership and no command; do not infer ownership from a coincident private-stat bit. | **Adopt for the first runtime slice.** Explicit schema/profile ownership remains intact. Private-stat inference needs a separate semantics proof before it can extend unknown entries. |
| Reuse the mutable catalog and generalize its existing collision-checked ID allocator. | **Adopt.** No second merge pass, registry, or parser. |
| Do not make `VR_WeaponCatalog_Observe` a gate or copy its diagnostic history. | **Adopt.** Its source return value is ignored by discovery; the target does not need an unconsumed parallel observation table. |

No human choice blocks this design. The first proof must also include a stock
fallback release after an unknown viewmodel reuses its selector and a same-map
serverinfo reset. Physical headset testing stays deferred.

## Implemented code checkpoint

The wheel now observes the equipped selector and a valid precached model after
full signon at the main-thread render boundary. Weapon-looking paths are matched
against the existing catalog using selector and g_/v_ path equivalence. One
unique roster/schema slot without an authored model can learn a copied path;
once learned, a different path is a separate discovered identity. Unmatched
models append active-only, unselectable rows with no guessed impulse or ownership
bit. Stock/profile rows retain their owned selection when a different observed
model reuses their selector. The active-match check accepts any valid model path;
the stricter weapon-name heuristic applies only when creating a new identity.

`CL_ClearState` and `CL_Disconnect` clear borrowed wheel model/frame references
before client state teardown, including same-map serverinfo resets. They keep
copied identities until game reload. The Linux debug build and `git diff
--check` pass. This is a code checkpoint: the unknown-model, stock fallback
release, schema learning, and map-restart scenarios still need end-to-end
qualification as part of final integration; no headset test is claimed.
