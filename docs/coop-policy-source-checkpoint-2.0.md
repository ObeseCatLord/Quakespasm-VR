# Current co-op profile, sharing and presentation source

2026-09-30. Bounded source comparison with current primary51b452c0. Source
presence is not final software qualification. Revival remains excluded.

| Feature | Current-source disposition |
| --- | --- |
| COOP-001 profile | Native host.c SV_CoopFeatureEnabled preserves modern/classic automatic selection and explicit finite overrides. Original cvar/init owners remain. Missing pickup-target and item-respawn controls are identified separately below. |
| COOP-002 collision/damage | Native legacy clip and temporary friendly-fire callback shield are present. Main confirmed server PMove lacked the advertised client exclusion; the collision plan repairs that gate and restores original telefrag impact conditions. Full damage/physics qualification remains pending. |
| COOP-003 accepted sharing | Main spot-checked current world.c SV_IsCoopSharedPickupCandidate, capture/gain predicates and SV_ShareCoopPickupInventory against primary. Astra compared fourteen gain/key-loss functions after native progscrc adaptation. Accepted ownership/progression deltas and counted keys remain; ammo can prove acceptance/duplicate ownership but is not team-copied. Trigger and solid-impact key consumption are connected to native QC. Broader field/callback/runtime behavior is unqualified. |
| COOP-004/005 target/regen | Source-integrated unchanged-target firing after accepted pickup and supplemental native SUB_regen scheduling reuse inherited helpers, typed snapshots, shared inventory acceptance, retained native touch and existing sticky lifetime ownership. Nine controls/five notify callbacks retain donor policy. Complete patch/source review found no P1/P2 blocker; final target-count and regeneration qualification remain pending. |
| COOP-008 relocation | Existing player/spawn wheel commands resolve server-side clients and safe native placements, clear old VR contacts, set QC fixangle and publish native discontinuity. Unsupported selector admission and same-slot callback recreation defects are source-repaired through the existing sticky cancellation owner and private scratch isolation; captured-player survival gates fallback/diagnostics/relocation. Final qualification remains open. |
| COOP-009 presentation | Current gl_rmain.c R_DrawCoopPlayerOutlines/filled silhouettes and wheel-selected silhouette reuse r_alias.c R_DrawAliasCoopOverlay and native prepared geometry/palettes. Stereo rings use existing late writable scene stencil; desktop keeps filled fallback. Native world-name glyphs are depth-tested: **outlines**, not names, show through walls, matching the inherited design. No alternate renderer or new pass is needed. Both-eye/format/transparency/MSAA visual qualification remains pending. |

The requested-local-Astra/max source advice changed the next implementation:
port the two actually missing native-QC pickup policies; repair the proven
client/server collision mismatch; reuse the existing callback cancellation
owner for selector/target callback lifetimes. Effective reviewer settings are
not exposed; this document does not certify a formal model/skill review.
Main verified the load-bearing source claims and retains final integration.
No builds, tests or runtime probes were performed for this checkpoint.

Relevant plans: [collision repair](coop-collision-source-2.0-plan.md),
[pickup target/regen](coop-pickup-target-respawn-2.0-plan.md),
[existing save/join owners](coop-save-source-checkpoint-2.0.md),
[co-op outline design](migration-coop-outline-design.md),
[friendly-fire design](migration-friendly-fire-review.md) and
[full Linux/ARM qualification](final-linux-arm-qualification-2.0-plan.md).
