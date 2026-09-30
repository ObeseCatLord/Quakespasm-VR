# Co-op native respawn policy adapter

2026-09-30. COOP-006, with COOP-013 as a separately bounded inherited lifecycle
follow-up. Verified base: 2.0 `7ce3479b`; primary behavior `51b452c0`. This is
implementation planning before code. Builds/tests remain deferred until the full
migration implementation pass is finished. The trace-revive patch owns the same
server files first; do not implement this concurrently with it.

## Intended behavior and references

Copy inherited modern co-op respawn-near/death-spot, cooldown and inventory
retention policy over normal mod QuakeC spawning. Defaults: near-player `-1`
with the existing modern/classic policy, delay10, existing keep-weapons/ammo
toggle. Safe near-death placement is preferred; living teammate candidates are
ordered by score and distance. Team wipe removes teammate fallback, not a safe
death spot. If no safe replacement exists, retain the mod's ordinary spawn.
Desktop and VR share server authority and native QuakeC attack/respawn timing.

Primary `sv_phys.c:3783–3927` is the actual pre/post-QC behavior, not a second
respawn implementation. Reuse its inventory merge, death anchors, cooldown
input suppression and native placement selection. `sv_phys.c:3158–3317` records
safe/death poses and supports dead-player changelevel encoding.
`sv_phys.c:3548–3713` provides candidate ordering, near-death/teammate selection
and joining-player placement. `sv_main.c:5102` prepares inventory before native
SetChangeParms; `host_cmd.c:2537–2538` places a fresh co-op joiner after QC.

COOP-013 is inherited QBJ3 lifecycle compatibility, not generic new abilities:
primary `sv_phys.c:2211–2597` identifies typed existing APIs, respects native
plunge/limbo ownership, completes a demonstrably stuck normal limbo callback,
and clears only orphaned void/expired berserk effects. Preserve the native mod
callback and inherited activation guards; no new mod whitelist or portal logic.

## Verified existing owners and missing behavior

At the verified base, a full Quake C/C++ source search finds no
`sv_coop_respawn_near_player`, `sv_coop_respawn_delay`, automatic placement,
PrepareChangelevel, or inherited limbo cleanup helpers. Existing manual teleport
and save/load inventory projection do not implement automatic respawn policy.

Native `sv_phys.c:665–878` already owns typed inventory save/merge/restore and its
last-alive cache. `sv_phys.c:869–912` owns once-per-world-frame death observation
and shared progression reconciliation. `sv_phys.c:4952–5150` already provides
floor/hazard checks, nearby search, teledeath cleanup, relocation and manual
teleport. Reuse these actual components. The pending trace-revive adapter may
factor relocation/context helpers; inspect its committed result before coding.

Three native PostThink paths and the existing world death tracker must remain
the sole callback/time/movement owners. Private prediction has command-level
input as well as shared world-QC input; donor raw button restoration alone is
not verified for this boundary. Native slot reset/map reset/abort owners and
edict retention already exist. Fresh joining may occur before client->spawned
becomes true; do not reuse a predicate that silently disables joining placement.

## Adapter compared with replacement

Choose a copied before/after-QC policy scope and bounded per-client safe/death
pose state. Extend existing inventory cache/reset and world death observation;
do not duplicate those arrays or introduce another client respawn state machine.
Actual incompatibilities are extra native PostThink owners, command input
borrowing, discontinuity publication and client lifetime. Adapt there only.
Replacing PutClientInServer, QC weapon behavior, prediction queues or networking
would multiply working policy and is rejected. No protocol or Vulkan edits.

Stage1: generic COOP-006. Copy reference anchors/ranking/cooldown/placement and
join/changelevel hooks, reusing current typed cache, safety and relocation.
Capture the true pre-death body pose once in the existing world tracker, including
world/projectile deaths outside PostThink. Preserve once-only shared key/death
reconciliation. Restore input only for the same live client/VM owner and preserve
actual QC impulse consumption and authored angle changes. Reuse movement
discontinuity without clearing accepted command history.

Stage2: COOP-013, after a separate source disposition. Copy only demonstrably
missing inherited mod-lifecycle guards/cleanup at the same respawn owner. Keep
native freeze/thaw and mod-owned teleport callbacks. Validate actual field types,
VM, client and destination lifetime around each callback. Inventory restoration
must not resurrect an expired QBJ3 fist viewmodel over fresh QC weapon state.
Do not make physical-contact melee a dependency of ordinary co-op spawning.

Expected stage1 write set: `sv_phys.c`, `sv_main.c`, `server.h`, `host_cmd.c`;
existing lifecycle hooks only if required. Estimate600–850 net lines mostly
copied source, with fewer lines if existing helpers cover donor bodies. Stop and
reopen above850, new owners or duplicate state machines. Stage2 estimate300–450
net lines; its final boundary must be reviewed before production changes.

## Open source decisions and final acceptance

Local Astra must resolve actual private/native callback ordering and input
suppression; safe/death snapshot lifetime and world-death tracking reuse;
fresh join/changelevel/saved-player scope; callback disconnect/bot reuse and
abort/reset; dry/ordinary water placement policy and inherited mod lifecycle
boundary. Reuse trace-revive helpers where they already solve the same problem.

Later Linux/ARM software checks use real native QC and local clients: cooldown
then ordinary respawn, inventory on/off, safe death spot, unsafe spot/teammate
fallback, team wipe/lone co-op, fresh join, dead changelevel, saved-player restore,
classic/disable and mixed desktop/VR input. Verify prediction teleport fencing
and intentional QC effects. Stage2 covers native QBJ3 freeze/thaw and orphaned
effects without clearing active berserk. User live headset/multiplayer and
performance trials remain deferred; no measurement is required for this plan.
