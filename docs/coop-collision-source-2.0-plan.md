# Shared native co-op collision and telefrag boundaries

2026-09-30. Verified before-code COOP-002 repair. Native physics and PMove,
existing server policy/stat, client replay and damage callbacks remain owners.
No tests/builds until all implementation; revival remains excluded.

## Current source and smallest repair

- Primary51b452c0 `world.c:249` skips actual client body collisions in modern
  co-op while preserving point/missile traces; native2.0 `world.c:631` already
  copies that legacy clip policy.
- Current `world.c:2715–2783` collects server PMove solids without that player
  exclusion; `sv_phys.c:9445–9450` and private collision preflight consume it.
  Current `pmove.c:2391–2393,2558–2561` skips other client slots under the
  existing advertised STAT_VR_COOP_POLICY/NO_PLAYER_CLIP. This is a verified
  client/server policy mismatch; no new stat or collision solver is needed.
- Existing `SV_IsActiveClientEdict` plus SV_CoopFeatureEnabled already defines
  server eligibility. Factor only the existing profile/client-pair predicate
  into a small static helper reused by native clip and PMove collection.
  Native clip retains its point/missile restrictions. PMove uses its actual
  player hull; retain point-hull collision by requiring at least one differing
  pmove.player_mins/maxs axis before excluding the pair. Never skip monsters,
  brushes, projectiles, world or inactive/non-client bodies.
- Primary `sv_phys.c:4005–4020` calls SV_ShouldSuppressCoopTelefrag in **both**
  native impact directions. Current2.0 only uses it for triggers; current
  SV_Impact lacks those two gates. Export that same existing world predicate
  through server.h and copy the two conditions; no new teledeath classifier
  or damage implementation. Preserve native retained-edict/lifetime checks,
  ordinary callback, sharing and friendly-fire behavior when not suppressed.

Replace neither physics nor protocol. Exact write set: `Quake/world.c` small
pair helper/clip and PMove gates plus existing telefrag linkage; `Quake/sv_phys.c`
two impact guards; `Quake/server.h` predicate declaration. Target<=30 net lines.
Main owns docs/CSV; one worker owns these precise regions. Reopen if a state,
solver, extra per-client policy or broader lifetime rewrite becomes necessary.

## Evidence limits and final acceptance

Main complete-diff review and scoped whitespace now; no executable checks.
Final Linux/ARM cases cover modern/classic/explicit no-player-clip overrides,
client/server standard/private PMove, actual point/missile traces, dead/inactive
bodies and world/monster collision. Telefrag tests cover both impact directions
and trigger touches, spawning before begin, same-owner/self damage and ordinary
enemy/world callbacks. Existing native server snapshot/replay and optional VR
roomscale must consume the same policy. A source gate is not gameplay proof.
