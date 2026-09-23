# Private VR server migration: Astra senior disposition

Reviewed 2026-09-22 against `2.0` and pinned OpenVR `1327f795`. The local
review ran with verified `gpt-6-astra` / `max` settings. This is a design gate,
not an implemented server-compatibility claim. The current client can fire at
the unchanged pinned dedicated server; `2.0` still routes local `clc_move` to
vkQuake's public reader despite having `SV_ReadPrivateUsercmd`.

## Decision

Keep vkQuake's transport, snapshots, QC lifecycle, collision and host cadence.
Admit an explicit per-connection private dialect through the existing `pext`
roundtrip and serverinfo, then adapt those owners only where the pinned wire
layout actually differs. The alternative of copying the inherited server
loop would duplicate public parsing, command lifetime and physics policy.

| Astra recommendation | Main disposition |
| --- | --- |
| Merge admission and version compatibility into one per-connection wire profile; never infer it from colliding PEXT2 bits. | **Adopted.** Use an explicit key/version in the existing `pext` exchange and a serverinfo marker before decoding private extensions. Keep `connect … qsvr1` as the current explicit legacy fallback. The local listen path must use the same handshake as dedicated, not a direct client-state shortcut. |
| Route the exact engine `pext` command before `SV_ParseClientCommand`. | **Adopted.** Current `sv_user.c` offers most client strings to QC first; a mod could swallow negotiation. Preserve QC handling for unrelated commands. |
| Include server-to-client framing in the first slice. | **Adopted.** Private signon `svc_time` has no inline ACK, while vkQuake emits one for PREDINFO. Private replacement snapshots require ACK metadata beyond vkQuake's short ACK. Adapt both serializers and split-packet accounting per connection; do not claim success from decoding `clc_move` alone. Start with legacy-frame authority and prediction disabled until owner/ACK coherence is demonstrated. |
| Use the donor's latest-command mode before PMove queue. | **Adopted as the first behavior proof, not final network parity.** Validate `msec` but do not add a simulation clock. Decode every complete bundled command, discard stale sequences only after consuming their bytes, accumulate each fresh roomscale delta once, retain newest axes/pose, and latch attack/jump plus last nonzero impulse until a real physics/QC pass. Reset on map/reconnect and clear stale input by arrival time. The eventual QSS-M prediction/PMove requirement remains open. |
| Port roomscale through the existing collision owner with controlled callbacks. | **Adopted.** Consume blocked/frozen tracking without later debt. Keep vkQuake solver/pushers and suppress duplicate impact/trigger QC callbacks for the extra physical translation. |
| Scope muzzle/aim across scheduled weapon thinks and PostThink, not just a vanilla fire call. | **Adopted.** Keep accepted-command authority for dedicated and listen. Preserve aim globals, source offset compensation, world clamp and body restoration while respecting QC `setorigin`; actual trace/damage is the proof. |
| Prefer a wholesale inherited server transplant. | **Rejected.** No demonstrated incompatibility requires replacing vkQuake's public network or physics owners. Reopen this decision only if the adapter begins duplicating their state machines. |

The first vertical proof is a `2.0` private client and dedicated server with a
public desktop observer: unobstructed body movement, wall-blocked and frozen
movement without debt, then a calibrated shotgun shot whose actual trace,
target damage, ammo and restored body origin match the accepted pose. Repeat
on listen server, redundant/lost bundles, map change and reconnect before
claiming this slice ready. A decoded packet, ACK counter or shell decrement
alone does not prove collision or muzzle behavior.

The reviewer flagged the QSS `0x80` collision as outside its scoped headers;
main verified it in pinned `QSS-M/Quake/protocol.h:75` (`PEXT2_INFOBLOBS`) and
private `svc57` versus QSS `svcdp_entities` at `:360`. The source tie is a
reason for explicit identity, not for changing vkQuake's public PEXT2 meanings.

No user choice blocks implementation. Supporting an unmodified inherited
client on a new `2.0` server is a separate compatibility expansion; current
`connect … qsvr1` support targets the unchanged pinned server.

## Negotiation checkpoint

The `2.0` client now offers a separate `QSVR` key/version in the ordinary
`cmd pext` response. The server records that offer per connection, and the
client parser accepts a matching serverinfo marker before private PEXT2 bits.
Public PEXT2 support and the default server response remain unchanged. The
server does **not yet select** the profile. Private outbound ACK/signon framing,
a latest-command receiver and a collision-solver roomscale adapter are staged;
their actual gameplay behavior still needs the coupled vertical proof above.
The extra client offer is ignored by public servers. This receiver is the
first compatibility step, not the requested QSS-M-style predictive netcode:
command replay, owner snapshot
coherence, loss recovery and VR-aware prediction remain required.

The inherited `0x40` private extension names a tagged `UF_SOLID` encoding.
vkQuake currently does not emit `UF_SOLID`, so selecting the private profile
must not be treated as proof of predicted collision parity. Keep prediction
disabled in the first proof and add a scoped solid writer only if the owner
snapshot or later prediction path actually needs it.

## Roomscale collision: Astra senior disposition

A second local `gpt-6-astra`/`max` review verified the inherited auxiliary
roomscale call and vkQuake's walk/step/pusher path before critiquing the
adapter. This is a design decision, not a collision-parity claim.

| Recommendation | Disposition |
| --- | --- |
| Reuse vkQuake's walk/fly solver with an explicit callback policy through its step, unstick and push calls. | **Implemented, unqualified.** Ordinary calls retain their current impact/trigger behavior. The auxiliary call suppresses QC callbacks but retains intermediate `SV_LinkEdict(false)` spatial updates. No copied solver or global suppression mode. |
| Apply roomscale before the normal move-frame capture. | **Implemented, unqualified.** A temporary auxiliary pusher frame is used, then velocity/flags/groundentity are restored and the entity relinked without triggers before normal capture. Reusing the old frame after leaving a platform risks stale support. |
| Use one accepted-command authority for remote and listen clients initially. | **Adopted with a parity caveat.** The donor's local direct tracking move runs later and has different callbacks/downward velocity. Measure local latency and behavior; a distinct local path needs evidence before addition. |
| Require one impact/trigger event globally. | **Rejected.** Native vkQuake stepping, unsticking and force-retouch can legitimately call more than once. The invariant is zero **auxiliary** QC callbacks while preserving the normal pass. |
| Fix the walk waterjump check's use of the last global `sv_player`. | **Adopted and implemented.** Read `ent->v.flags` for the entity being moved; otherwise another client's waterjump could affect stepping. |

The next adapter proof covers floor and wall movement, accepted/rejected steps,
startsolid, platform departure/boarding and fast pushers; frozen/inactive input
must leave no tracking debt. Compare dedicated and listen sessions with donor
remote/latest behavior, and count auxiliary versus normal QC callbacks.
Auxiliary-only solid impacts and thin crossed triggers are explicit caveats to
test, not reasons to add a new contact queue before evidence.

## Weapon pose scope: Astra senior disposition

A local `gpt-6-astra`/`xhigh` review compared the staged generic weapon scope
with the pinned OpenVR donor and vkQuake's QuakeC and spatial-link owners. It
found two correctness issues before private-profile activation. The review was
static; a successful build is not gameplay proof.

| Finding / recommendation | Disposition |
| --- | --- |
| An equality guard on `setorigin` can erase a QC teleport after `self.origin` was assigned first. Preserve every explicit `setorigin`, as the donor does. | **Fixed.** `PF_setorigin` marks relocation for the scoped player regardless of its current mutable origin. Other entities do not alter that scope. |
| `setsize`, `setmodel` or another scoped relink can index the temporary hand origin; restoring the body alone leaves stale absolute bounds, area links and PVS leaves. | **Fixed at the shared link owner.** `SV_LinkEdict` records a relink only for a scoped entity. Scope exit re-links the restored body without trigger callbacks, while retaining QC's size/model changes. It does not re-link when an explicit `setorigin` must be preserved. |
| Replace vkQuake physics/QC cadence or add a second local weapon policy. | **Rejected.** The accepted command supplies both local and remote VR pose. vkQuake movement and QuakeC callbacks remain the behavioral owners. |
| Reuse donor source compensation and load weapon calibration on dedicated servers. | **Adopted, still unqualified at runtime.** The helper uses the single weapon schema and the donor's default 8-forward/16-world-up source, grenade self-origin and schema overrides. |
| Activate the private profile now. | **Deferred.** The coupled receiver, collision, weapon, snapshot/ACK and prediction proof remains incomplete. |

Before activation, exercise a shotgun shot against a target and verify trace,
damage, ammo, cadence and restored body state. Cover scheduled continuous fire,
grenades, schema pitch/roll, wall-clamped muzzle, dedicated game switching,
callback-error unwind, and QC `setorigin` after direct assignment in both
Think and PostThink. Also change player size/model during a displaced PostThink
and immediately verify collision bounds, area membership and visibility leaves
without extra trigger callbacks. These checks should run against the pinned
donor where its behavior is the reference.

## Private snapshot collision bounds checkpoint

The private replacement-snapshot writer now derives `UF_SOLID` from the
authoritative edict bounds and uses the pinned donor's tagged solid encoding.
The explicit `QSVR` profile gates both the delta bit and the tagged payload;
public vkQuake snapshots and baselines retain their existing layout. Owned
entities are non-solid to their owner, as in the donor. This supplies the
client collision collector with a server source. The profile is now admitted
only on opt-in servers, but moving-solid writer/decoder roundtrips have not
been qualified.
Before enabling prediction, verify moving and changing solid boxes, BSPs,
owner exclusion, packet loss and reset baselines through a live private
snapshot, alongside the accepted-command owner and ACK association.

## Predictive server movement: Astra senior disposition

A local `gpt-6-astra`/`max` review checked the proposed QSS-M-style private
server adapter against pinned QSS-M `03a498aa`, inherited OpenVR `1327f795`
and the current `2.0` owners. Prediction must be defined by executed commands
and coherent owner snapshots, not by matching either donor's wire bytes.

| Recommendation | Disposition |
| --- | --- |
| Keep public vkQuake physics, transport and QC cadence; add a private movement adapter at the existing player owner. | **Adopt.** QSS-M's solver is already linked for client replay. A wholesale inherited server transplant would import unrelated co-op/Gorilla/mod policy and duplicate working public state. Reopen if the narrow adapter begins reproducing most donor policy. |
| Preserve ordered accepted commands separately from completed or explicitly discarded commands. | **Adopt.** The current latest-command receiver advances `lastmovemessage` on reception and is only a legacy-frame path. PMove cannot acknowledge that sequence as simulated until its physics pass has actually retired it. Bound queue count and total duration; clear it on map, reconnect, pause/discontinuity and overflow without replay debt. |
| Keep both frame roomscale sweep and PMove roomscale movement. | **Reject.** Only the selected movement owner may consume each accepted tracking delta. Likewise, `SV_ClientThink` must not apply native acceleration before PMove. |
| Treat an ACK and arbitrary owner delta as sufficient to permit client replay. | **Reject.** The current client gate requires a matching reset-decoded owner update, accepted authority/epochs/settings and prediction permission. Reserve snapshot space and associate the ACK with completed commands; local listen prediction needs a real server completion rather than a synthetic local ACK. |
| Preserve attack/jump/impulse as bits only while replacing pose with the latest command. | **Reject for PMove.** Retain the originating accepted command so QuakeC weapon callbacks use its hand pose. Legacy latest-command latches remain limited to the legacy owner. |
| Select the private profile for an opt-in legacy-frame proof before enabling PMove prediction. | **Adopt as an explicit qualification checkpoint.** Prediction remains disabled. Profile selection still requires exact marker/layout, private sender/receiver, weapon and collision behavior, signon and snapshot proof with public observers. |

The first implementation slice is a bounded private accepted-command queue in
`server.h`/`sv_user.c`, with separate receive and retirement cursors, exact
duration/action/pose/roomscale records and explicit discard cutoff. It must not
activate prediction by itself. The execution consumer then adapts vkQuake's
player physics/QC owner, followed by matching movement stats, reset owner
snapshots and ACK/epoch publication. Empty queues must remain under the chosen
PMove owner instead of silently switching back to legacy physics. Validate the
whole path locally with jump/swim/platform/contact, earliest-pose weapon damage,
once-only roomscale, packet loss, pause/teleport, listen and remote peers, and
mixed public/private clients. Custom-physics mods are a separate compatibility
gate with prediction disabled until their authority can be stated exactly.

## Explicit legacy-profile admission checkpoint

The server now has a default-off `sv_qsvr_private` opt-in. It selects the
private profile only after an explicit version-1 `QSVR` offer and matching
replacement/PREDINFO capability on an RMQ float-coordinate/short-angle map.
The selected private mask is separate from the retained public offer, so a
later map can return to public framing. Public-only and default-off peers keep
vkQuake's original profile. No predictive authority is advertised.

An isolated Linux loopback test with the `2.0` client and dedicated server
using disposable Straight asset profiles reached signon and observed
authoritative movement, shell consumption and advancing ACK with prediction
permission off. The same client reached public signon and moved/fired when the
server cvar was off. A connected private client also completed `e1m1` to
`e1m2` changelevel after the cvar switched off and parsed the new public
profile. The focused calibration fixture passed ASan/UBSan and the strict
Linux Ninja build passed. These checks do not prove target damage, roomscale
collision, ordered PMove prediction, mixed simultaneous peers, packet loss,
headset input, Windows or ARM behavior.

## Private legacy ACK retirement checkpoint

The private replacement-snapshot ACK now reports a separate completed-move
cursor. The player physics owner captures the latest accepted sequence at
entry and publishes it only after its QuakeC PostThink returns for a live,
spawned owner. Receive-time `lastmovemessage` remains the deduplication cursor;
public PREDINFO ACK framing is unchanged. Both cursors reset on new signon,
replacement-frame setup and client connection. This closes the known case
where a received command was acknowledged before a player physics pass, but
it does not associate a queued command with a predictive PMove step. Prediction
permission remains off. The strict Linux `vkquake` target builds; a focused
runtime stall probe remains to be run. A fresh private-profile loopback run
after this change reached signon, moved 267 units, consumed four shells and
advanced the bounded ACK from 69 to 284 while prediction stayed disabled.
That observes normal frame retirement, not the no-physics stall case.

## Bounded accepted-command journal checkpoint

The explicit private receiver now retains each fresh validated `usercmd_t` in
a 32-entry, 250 ms per-client journal, preserving its sequence, action, VR pose,
roomscale delta and server receipt time. A separate receive cursor still owns
wire deduplication. Long arrival gaps, suspended gameplay, non-moving owners,
overflow and level/connection resets discard pending journal entries at an
explicit cutoff. After a legacy player physics pass, the journal retires
records through the completed cursor and releases their storage. This is
bookkeeping under latest-command authority: one frame may retire multiple
records even though they were not each individually simulated. The PMove
consumer must replace that retirement rule before granting prediction.

The strict Linux `vkquake` build and a fresh private-profile loopback smoke
passed after the journal change: signon, settled movement, four consumed
shells and a bounded advancing ACK with prediction permission off. Queue
overflow/discontinuity and per-command PMove behavior still need focused
qualification; this is not a prediction or headset-parity claim.
