# Shared wheel data source checkpoint

2026-09-30, primary `51b452c0` and current source inspected directly. This records
WPN-003/004/006 owners; earlier inventory placeholders do not imply those owners
need another implementation. Native vkQuake retains command/stat/filesystem and
draw ownership. Source evidence is not full feature or runtime acceptance.

| Boundary | Actual inspected production owner | Remaining proof |
| --- | --- | --- |
| Ownership and discovery | `VR_WeaponMenu_AddSchemaEntry` admits declared rows only with valid command/identity and complete descriptor pairs; a new implicit bitmask row uses `STAT_VR_WEAPONS`. `VR_WeaponMenu_CatalogEntryOwned` supplements that with item bits only for the known stock-bit subset. Explicit ownership metadata removes the supplement. `EntryOwned` makes discovered rows active-only; discovery never invents an impulse. `EntryActive` checks observed held identity where required, retaining owned stock fallback selection under selector reuse. | Actual declarations/private stats, custom high bits, ambiguous identity, transient activation and unknown model cases through real native transport and release owners. No arbitrary mod correctness claim. |
| Quantities, capacities and readiness | `SV_CalcStats` produces currentammo and independent reserves. `SV_WriteAmmoCapacityStats` reads one coherent field/global family and validates finite/integral/positive capacity values. The wheel derives capacity metadata from the final ammo type and resolves positive runtime maxima over fallbacks. `EntrySelectable` keeps unknown magazines selectable, while ordinary inactive reserve rows require ammo. The [equipped-currentammo repair](weapon-wheel-currentammo-2.0-plan.md) hides unrelated equipped ammo under an inactive magazine row. | Actual active/inactive magazine, reserves, empty/unknown quantities, dynamic capacity upgrades, packet/stat transitions and both draw paths. |
| Schema/roster precedence and lifetime | `VR_WeaponMenu_ReloadGame/LoadSchema` load through native `COM_LoadFile`, require the active search-path identity, free loaded buffers and apply the existing bounded parser. A valid supplied roster remains authoritative; declared metadata enriches compatible slots. Without a roster, native stock/profile/schema/discovery precedence remains. Complete declarations suppress guesses, including an empty complete roster. Client reset clears borrowed frame/model references before teardown; game reload clears copied catalogs. | Actual active/inherited files, malformed/partial/complete declarations, authored overrides, reload/same-map reset and worker lifetime qualification. |

These paths reuse the previously implemented [primary-delta adapter](migration-wheel-primary-delta-2.0-plan.md),
[discovery seam](migration-weapon-runtime-discovery-review.md) and
[wheel owner](migration-weapon-wheel.md). The new currentammo condition is a
demonstrated reference regression repair, not a second inventory/cache/protocol.
No builds/tests/probes were run. Consolidated Linux/ARM checks remain required
after all implementation; user headset/gameplay and performance trials remain
later follow-up. This checkpoint does not close the whole wheel/calibration map.
