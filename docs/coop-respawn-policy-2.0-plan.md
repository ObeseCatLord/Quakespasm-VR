# Co-op native respawn policy adapter

2026-09-30. COOP-006, with COOP-013 as a separately bounded inherited lifecycle
follow-up. Verified base: 2.0 `7ce3479b`; primary behavior `51b452c0`. This is
implementation planning before code. Builds/tests remain deferred until the full
migration implementation pass is finished. Co-op revival is excluded by the user;
this policy must reuse native helpers directly, with no revival dependency.

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
teleport. Reuse these actual components; factor them narrowly only where the
respawn contract needs explicit death angles or borrowed input restoration.

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
Capture a finite frame-start/body-owner fallback in the existing world tracker,
including world/projectile deaths outside PostThink. This is not an exact hook
at arbitrary QC damage. Preserve once-only shared key/death
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
boundary. Reuse the native lifecycle, placement and input helpers directly.

Later Linux/ARM software checks use real native QC and local clients: cooldown
then ordinary respawn, inventory on/off, safe death spot, unsafe spot/teammate
fallback, team wipe/lone co-op, fresh join, dead changelevel, saved-player restore,
classic/disable and mixed desktop/VR input. Verify prediction teleport fencing
and intentional QC effects. Stage2 covers native QBJ3 freeze/thaw and orphaned
effects without clearing active berserk. User live headset/multiplayer and
performance trials remain deferred; no measurement is required for this plan.

## Adopted local Astra source disposition

Requested Astra/max read the pinned source; effective settings were not exposed,
so this is source advice rather than formal review certification. Main checked
the donor begin-before-PreThink ordering, native continuation skips, typed void
bypass and join distinctions. Reopen the following contracts before code:

| Finding | Disposition |
| --- | --- |
| Donor BeginPostThink actually precedes PlayerPreThink | Adopt: carry one existing policy scope from before PreThink through any continuation; finish once after native pose/FF/input unwind. Filter all actual QC/button owners during cooldown, including shared-QC rebinding, without changing accepted queue records. |
| Retention/pointer equality cannot identify client-slot reuse | Adapt to revival exclusion: use native lifecycle hooks and a bounded borrowed respawn-policy scope, without another identity registry. Clear transient anchors/timers/wipe state in current slot/map resets and cancel borrowed input restoration before disconnect/free/reuse/abort. |
| Typed plunge/void bypass is necessary for generic policy | Adopt stage1 passive ModOwnsLifecycle bypass from actual reference. Defer active recovery/orphan-effect callbacks to stage2. Neither physical-contact melee nor a new mod whitelist is a prerequisite. |
| Frame-start fallback is not the exact arbitrary QC death position | Adapt: retain a finite body-owner/frame-start fallback; capture inventory before StartFrame can strip it. Update only from surviving restored body owners, not temporary weapon poses or stripped corpses. Preserve existing once-only shared reconciliation. |
| Fresh joins run before spawned and map-initial joins differ | Adopt: copy the reference initial-spawn distinction into native client/server bookkeeping; use current saved-client identity to preserve living saves and distinguish dead-saved/newcomer placement. Native64-parm changelevel extraction remains. |
| Null teammate anchor, force_retouch and discontinuity need adaptation | Adopt: factor native relocation narrowly for explicit death angles; death candidates stay dry and teammate candidates obey existing water policy. Restore current invocation's retouch only for committed relocation. Native QC fallback retains spawn effects. Publish native discontinuity even for ordinary successful respawn, without retiring queues. |
| Numeric conversion/timers can invalidate optional policy | Adopt finite/range checks. Nonpositive/nonfinite delay never locks input indefinitely; invalid candidate geometry/ranking leaves native spawning intact. |

The estimate remains600–850 net lines, not a target to fill. Native callbacks,
inventory, input scopes, placement and shared world-death ownership remain.

## Performance fork requiring disposition before coding

`SV_CoopRespawnTouchesHazardTrigger` currently scans every non-player edict for
every candidate (`sv_phys.c:4930–4950`). It is currently used by infrequent manual
placement. Copying donor safe-origin sampling before/after every private command
would put repeated whole-map scans on a high-frequency path, even for living
players on jumbo maps. This is verified source cost structure, not a benchmark.

Native `world.c:352–385` already traverses linked trigger area nodes for overlap;
`SV_TouchLinks:2429` consumes that collector. Prefer reusing this spatial owner
for a read-only bounded overlap predicate, without executing touch QC, changing
links or introducing a hazard cache. Consider extending the private traversal
with optional predicate/bounds inputs and a narrow public query; preserve current
touch list collection unchanged. Fixed-size truncated candidate lists and dummy
live-edict relinking are rejected because they can change placement correctness.
Alternatively retain the scan but sample safe anchors only at existing world-body
boundaries; reviewer must assess whether that loses useful inherited behavior.

Open: whether that small `world.c`/`world.h` boundary is justified, sampling rate,
unlinked-trigger equivalence and callback-free traversal ownership. Stage1 must
not start by quietly accepting per-command whole-world scans. A chosen extension
requires explicit write-set revision before production changes and final
software checks comparing trigger/hazard eligibility and relocation behavior.

## Adopted spatial-query disposition

The bounded local Astra source assessment selected a40–80-net-line extension of
the existing area traversal. Main verified the native traversal and TouchLinks
call, the raw candidate bounds, and native LinkEdict split/expansion rules.

| Recommendation | Disposition |
| --- | --- |
| Whole-world scan repeats for each accepted candidate/body-owner sample | Adopt the native spatial trigger owner; no hazard cache or second index. This is source scaling evidence, not a measured latency result. |
| Linked traversal cannot include stale unlinked trigger bounds | Adopt linked active trigger semantics explicitly, matching native touch processing and documented relinking requirements. Unlinked/inactive stale bounds no longer veto optional placement. Do not claim arbitrary QC-edited unlinked edict equivalence. |
| Preserve native TouchLinks collector behavior | Extend its private traversal with explicit bounds and an optional pure C predicate; list mode retains exact ordering/capacity/self filtering. Query mode early-exits, allocates no candidate list and executes no QC. |
| Player candidate bounds must not gain linked-bound expansion | Pass origin+mins/maxs exactly; retain trigger-side absolute bounds and inclusive overlap. Reject invalid bounds/world state conservatively for optional placement. |
| Do not downgrade body sampling to world-frame-only for cost | Retain body-owner sampling. An optional unchanged-safe-record shortcut may compare origin, angles and view angles together, never cache safety or skip later candidate validation/inventory. |

Production slice ownership is `Quake/world.c`, `Quake/world.h` and only the
existing hazard predicate/caller in `Quake/sv_phys.c`. Target40–80 net lines;
reopen above100, a new cache/index, or native touch/QC behavior changes. It is
sequenced before generic respawn integration; no revival helpers exist or are
required. Stage1 may subsequently edit other sv_phys regions after this slice
is reviewed and committed. No builds/tests/fixtures until full implementation.
Final software cases include linked hazards, split/bound equality, dense trigger
lists, relinking, unchanged ordinary touches and placement refusal/fallback.

### Spatial-query source checkpoint

The integrated three-file slice is33 net lines. It extends the existing private
collector with explicit bounds and predicate mode; the sole TouchLinks caller
still passes its original linked bounds/list/capacity and no predicate. List
mode preserves traversal/filter/order/capacity, retention and native QC handling.
Query mode early-exits with no list allocation or QC; hazard classification
retains the original non-player/free/solid/touch/classname rules. Public invalid
world/bounds admission conservatively refuses optional placement. Main compared
the full patch and native/reference owners; scoped git diff --check passes.
No tests/builds/probes were run. Final linked-hazard/touch/placement software
qualification remains pending. Generic respawn policy itself is not implemented
by this slice, and no revival code was added.


## Adopted shared-QC lifecycle disposition

Local Astra source advice verified the native think-window and continuation
owners at `5af27619`; main spot-checked the current unchanged owners after the
spatial prerequisite. Requested Astra/max settings remain unobservable through
the agent API, so this records source advice, not formal review certification.

| Finding | Disposition |
| --- | --- |
| Shared PreThink/PostThink spans several accepted command heads | Adopt bounded policy data by value in `sv_client_think_window_t` only for `shared_qc`. Other lifecycles use stack-local state. The existing `available` flag owns weapon Think scheduling, never policy liveness. |
| Continuation phases skip callbacks already executed | Adopt an explicit policy pointer through the existing continuation signatures. Capture once immediately before original PreThink; never recapture at AFTER_PRETHINK or AFTER_WEAPON_THINK. Fresh native/cooperative owners use their own scope. |
| Command completion is not PostThink completion | Adopt an explicit actual-PostThink flag. Finish after friendly-fire/weapon-pose/temporary-input unwind and before inventory refresh. Intermediate shared heads retain policy; maintenance can finish without an ACK. Cancel missing/aborted tails instead of inventing callbacks. |
| Input borrowing is shorter-lived than shared policy | Adopt per-invocation input exposure and native input-save machinery. Filter after every final staging/rebinding before relevant QC; preserve accepted records and shared snapshots. Never resurrect consumed impulses or overwrite QC-authored angles. Use server QC time for cooldown. |
| A shared window may exit via several loop breaks | Adopt one outer-window cleanup before its stack expires. Failure, selection loss and continuation failure cancel without retention/relocation. Local input/global borrowing must still unwind at intermediate heads. |
| Retention cannot distinguish reserved client-slot reuse | Adopt one transient cancellation binding to the current enclosing scope, no per-client identity registry. Cancel before disconnect QC, connect-slot memset, player ED_Free, server VM clear and nonlocal host abort. Validate the captured VM/client/edict and explicit cancellation flag before restoration. Clear borrowed pointers before nonlocal unwind. |
| Existing temporary writebacks can also affect reused slots | Adapt adjacent input/weapon-pose cleanup and old-invocation ACK/drop guards narrowly. Teardown must unwind VM globals while suppressing writes/completion/drop to a replacement owner. No general movement or callback rewrite. |

The stage1 write set explicitly includes `Quake/host.c` teardown/abort hooks
and `Quake/pr_edict.c` ED_Free/server-VM-clear hooks, in addition to
`sv_phys.c`, `sv_main.c`, `server.h` and `host_cmd.c`. These are demonstrated
incompatibilities at existing lifecycle boundaries, not a new service or
persistent registry. Existing reset/cache/world-death owners remain authoritative.
The600–850-net-line estimate still applies; stop for main architecture review if
it is exceeded, if another state machine is needed, or if narrow cleanup guards
become a broad callback rewrite. No revival dependency or runtime checks.
