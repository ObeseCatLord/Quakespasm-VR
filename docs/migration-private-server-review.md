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
server does **not yet select** the profile. Private outbound ACK/signon framing
and a latest-command receiver are staged, while collision and QC authority
still need the coupled vertical proof above. The extra client offer is ignored
by public servers. This receiver is the first compatibility step, not the
requested QSS-M-style predictive netcode: command replay, owner snapshot
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
| Reuse vkQuake's walk/fly solver with an explicit callback policy through its step, unstick and push calls. | **Adopted for implementation.** Ordinary calls retain their current impact/trigger behavior. The auxiliary call suppresses QC callbacks but retains intermediate `SV_LinkEdict(false)` spatial updates. No copied solver or global suppression mode. |
| Apply roomscale before the normal move-frame capture. | **Adopted.** Use a temporary auxiliary pusher frame, restore velocity/flags/groundentity, relink without triggers, then capture the normal frame at the new origin. Reusing the old frame after leaving a platform risks stale support. |
| Use one accepted-command authority for remote and listen clients initially. | **Adopted with a parity caveat.** The donor's local direct tracking move runs later and has different callbacks/downward velocity. Measure local latency and behavior; a distinct local path needs evidence before addition. |
| Require one impact/trigger event globally. | **Rejected.** Native vkQuake stepping, unsticking and force-retouch can legitimately call more than once. The invariant is zero **auxiliary** QC callbacks while preserving the normal pass. |
| Fix the walk waterjump check's use of the last global `sv_player`. | **Adopted and implemented.** Read `ent->v.flags` for the entity being moved; otherwise another client's waterjump could affect stepping. |

The next adapter proof covers floor and wall movement, accepted/rejected steps,
startsolid, platform departure/boarding and fast pushers; frozen/inactive input
must leave no tracking debt. Compare dedicated and listen sessions with donor
remote/latest behavior, and count auxiliary versus normal QC callbacks.
Auxiliary-only solid impacts and thin crossed triggers are explicit caveats to
test, not reasons to add a new contact queue before evidence.
