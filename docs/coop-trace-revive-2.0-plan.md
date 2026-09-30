# Native-trace co-op revive adapter

2026-09-30. COOP-007 source gap, verified before implementation. This is
expanded co-op through ordinary QuakeC melee traces, independent of the deferred
physical-contact damage solver. Desktop and VR share server authority; gestures
already produce ordinary attack input. Do not add another melee protocol or
per-mod attack implementation. Requested-local-Astra source/design disposition
must precede production changes; no builds/tests now.

## Behavior reference and actual gap

Primary `51b452c0` `sv_phys.c:1709–1968` contains trace-based corpse revival,
`sv_main.c:162–164` defines revive/health/range controls, `pr_cmds.c:1464` observes
ordinary traceline, and both native/queued PostThink paths bracket observation
and apply after the QC weapon callback and temporary VR pose return. The feature
handles SOLID_NOT corpses using padded dead-player bounds, selects the nearest
unobstructed short player-owned trace, runs normal PutClientInServer, restores
corpse position/angles, applies configured health and links the revived body.
Defaults are modern co-op auto-on (`-1`), health25, range96; classic co-op opt-out
and explicit disable use the existing feature/default policy.

Current2.0 full Quake C/C++ source search has no revive controls or these
helpers/calls. Native `sv_phys.c:472–491` already has active/dead/alive player
predicates; inventory, shared pickups, friendly-fire, safe relocation and
private movement discontinuity owners exist. They are reusable, not proof that
revive is already implemented. The source gaps therefore change the next action
for COOP-007 rather than warrant a checkbox-only inventory update.

Native PostThink runs at `sv_phys.c:8895/9378/9993` for maintenance, accepted
command and native continuation. Each restores friendly-fire and temporary
weapon-pose scopes. `pr_cmds.c:793–846` owns normal/VR-adapted trace dispatch and
QC trace publication. `SV_RunPrivateVRWeaponThink:7439` separately scopes due
native weapon Think. Scheduled melee traces outside PostThink are an explicit
unknown: primary's observation gate covers only PostThink, so do not silently
advertise support for all delayed custom melee through an unverified hook.

## Minimal design and review decisions

Copy/adapt primary slab intersection, padded corpse bounds, line-of-sight,
nearest-trace pending selection and ordinary PutClientInServer completion at
existing native server/QC owners. Reuse actual native client predicates, classic
feature policy, inventory and respawn teleport/discontinuity helpers. Retain
native trace globals and existing VM/physics/command timing; do not replace the
three PostThink owners with another client state machine.

Open decisions to resolve from source before coding:

1. Exact transient begin/end/apply boundary at all three PostThink callsites,
   including friendly-fire, pose restoration, callback removal and prediction
   publication. Whether the existing scheduled weapon Think owner also needs
   the same bounded observer for ordinary stock delayed melee.
2. Pending attacker/target lifetime across QC: use existing edict retention and
   actual client identity/liveness checks; revalidate after PutClientInServer.
   Reset on native map/client lifecycle rather than an independent registry.
3. Borrowed QC globals/spawn parms: native extended parms and actual execution
   scope differ from primary. Preserve caller globals, trace state and client/
   player identity through existing owners; do not copy a16-parm-only boundary.
4. Placement/teledeath/inventory/discontinuity: preserve intended corpse revive,
   avoid leaving a solid player embedded when all candidate offsets are blocked,
   and use native relocation/input-history reset. Inventory restoration must
   follow the inherited co-op policy, not an invented second save store.
5. Finite range/health/trace admission and ordinary desktop behavior; keep primary
   short-trace semantics instead of guessing weapon names or QC program hashes.

Expected production write set: `sv_phys.c`, `sv_main.c`, `server.h`, `pr_cmds.c`;
at most a narrow native spawn-parm/helper exposure if source requires it. Rough
budget300–400 net lines, largely copied primary logic. Reopen before more than
400 lines, additional owners or duplicated protocol/respawn/QC state. No new UI
page is justified merely to expose already-native console co-op controls.

## Final software acceptance, deferred until full implementation

Later Linux/ARM checks cover two real local clients with native QC: dead corpse
short trace, obstruction/out-of-range/disable/classic refusals, nearest target,
configured health, delayed stock melee if adopted, target/attacker callback
removal, corpse placement refusal/fallback, inventory, prediction relocation,
map reset, and mixed desktop/VR ordinary attack input. Verify original trace
globals and borrowed VM/client state survive callback completion. Existing native
damage remains QC-owned. User live multiplayer/headset trials remain separate.

This plan does not close COOP-006's broader respawn-near/cooldown policy or the
full co-op inventory. Those retain independent source and final software gates.

## Adopted local Astra source disposition

The requested Astra/max review verified the actual source before advising. The
tool exposes requested settings, not independently certified effective settings;
this is source/design advice, not final runtime or formal model certification.
The main thread spot-checked native Think scheduling, all three PostThink scopes,
client-slot reset owners, extended spawn-parm loading and selective QC restoration.

| Recommendation | Disposition |
| --- | --- |
| Include scheduled Think because stock axe damage is delayed | Adopt: observe the existing SV_RunPrivateVRWeaponThink callback as well as three PostThink sites. Do not add a weapon/mod dispatch gate. |
| Apply after borrowed pose, friendly-fire and command/angle scopes unwind | Adopt: apply before movement publication/inventory refresh; cancel on cleanup exits and recompute attacker ownership after spawn. |
| Retention alone cannot distinguish reused clients | Adopt: retain candidate and attacker, cancel on disconnect before its QC callback, on connect before slot reuse, on map reset and host abort. Cancellation remains visible during spawn. No separate identity registry/generation protocol. |
| Copying donor blocked-origin fallback embeds a living player | Adapt: try the actual spawned hull at corpse offsets with native floor/hazard checks. If none is safe, preserve ordinary PutClientInServer position, hull, linking and teledeath behavior. Do not force a blocked corpse origin or speculate an edict/world rollback. Even an unsafe authored spawn remains native QC's existing outcome, not a new engine placement guarantee. |
| Native has 64 spawn parms and additional borrowed QC state | Adopt: preserve applicable basic/extended parms, scratch/argc, trace/basis, self/other/time/frametime and existing cooperative input scope. Restore selectively only when saved VM storage is still valid; keep intentional gameplay globals. |
| Revive must explicitly restore target inventory | Adopt: merge the existing last-alive/corpse typed inventory before QC, restore it under the inherited toggle, apply existing shared progression, then refresh the existing cache. No second inventory store. |
| Relocation needs actual movement discontinuity and oldorigin | Adopt: reuse/factor native relocate and teledeath helpers with explicit saved corpse angles; update oldorigin and target discontinuity without retiring its command queue. |
| Observe actual trace semantics and exclude synthetic contacts | Adopt: keep dispatch/publication unchanged, admit only original finite short traces and ordinary native/VR traces; preserve donor padded slab bounds, obstruction and minimum intersection-fraction selection. |
| 400-line cap understates native adaptation | Adopt: reopen before coding to an estimated 450–650 net production lines across the four planned files and small host.c lifecycle hooks. Existing input helper may move within sv_phys.c rather than be duplicated. Stop for main architecture review before 650 net lines, another owner, or a second callback/respawn state machine. |

Ordinary QC spawn fallback is an explicit improvement over donor forced blocked
placement. Revival runs PutClientInServer once, never ClientConnect, with its
observation closed. This remains a bounded pending trace operation, not a general
QuakeC transaction or physical melee implementation. Final software acceptance
must cover delayed axe, connection/bot reuse, borrowed context, both placement
outcomes and native damage; builds/tests remain deferred until full implementation.
