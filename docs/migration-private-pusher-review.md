# Selected private movement on moving BSP platforms

Date: 2026-09-27. Branch: `2.0`. Astra Max senior design review of the
selected stock-QuakeC movement owner, followed by source spot-check and a
narrow implementation. The selected trial remains default-off.

## Behavioral reference and architecture

The selected owner executes queued input through a complete per-command QuakeC
lifecycle (`Quake/sv_phys.c`, `SV_Physics_ClientPrivateWalkTrial`); its queue is
bounded to 32 records and 250 ms (`Quake/server.h`). In the world loop, client
slots run before BSP pushers. vkQuake's `SV_PushMove` already carries players,
records persistent support, subtracts support motion already applied by other
pushers, and restores player origin/support on a blocked push. Its robust mode
is `sv_gameplayfix_elevators >= 3`, the default. The private PMove command
should see the current platform as collision geometry, while the existing
world-frame pusher remains the sole owner of platform displacement.

| Senior recommendation | Disposition |
| --- | --- |
| Admit pusher ground and body touch together in selected admission, state validation and PMove, conditional on robust elevator mode. | **Adopted.** The previous rejection at any of these three boundaries could disconnect an owner after a valid command or before a snapshot. Gorilla **palm** contact with a moving BSP remains excluded; that hand solver has a different support contract. |
| Keep vkQuake's `SV_PushMove` for carry; avoid a general native-frame handoff or per-command pusher simulation. | **Adopted.** Those alternatives duplicate command/callback ownership or a pusher clock. The existing terminal-state native adapter does not solve ordinary WALK/platform interaction. |
| Withhold client replay while a pusher grounds, touches or carries the player, including a later push in the same world frame. | **Adopted, conservative.** A frame-local interaction bit is cleared at the client physics entry and set at selected state/PMove and native pusher boundaries. The existing `MOVEACK_FLAG_PREDICTION_ALLOWED` check consumes it. An idle BSP pusher ground also withholds replay, since its later motion is outside the command replay. Completed-command ACK and authority are unchanged. |
| Guard a Gorilla ACK against a native push after the private command stored its hand state. | **Adopted.** The snapshot emits Gorilla state only when its stored origin is finite and within the existing 0.01-unit callback-movement tolerance of the final edict origin. This fences stale publication without clearing accepted hand state. |
| Add a second movement protocol or pusher state machine. | **Rejected.** Existing selected authority, equal-ACK permission updates and vkQuake support records cover the needed boundary. |

The review corrected an error in the draft brief: the selected queue limit is
**250 ms**, not 500 ms. Source anchors: `Quake/server.h` queue constants;
`Quake/sv_main.c` `SV_PrivateWalkTrialAdmissionFailure`,
`SVFTE_WriteEntitiesToClient`, and `SV_SendClientDatagram`; `Quake/sv_phys.c`
`SV_PrivateWalkTrialStateError`, `SV_Physics_ClientPrivateWalkTrial`,
`SV_PushMove`, and `SV_Physics`.

## Qualification still needed

This source-level change and a Linux build cannot prove native movement parity.
The next review found and fixed a remaining packet-gate rejection in
`SV_PrivateWalkTrialStateValid`: it now permits pusher ground only in the same
robust elevator mode as admission and physics. The regression proof must send
a new command **after** the lift has become the client's ground entity.
The focused software proof is one stock lift with an ordinary player and a
selected remote player: compare one command plus one push, no-command carry,
queued commands, departure, a blocked push/rollback, snapshot ACK and replay
permission, and the next raw-hand command after carry. Check origin and support
as well as callback count. It can run after the wider implementation work; the
user will handle live headset, eye tracking and performance tests separately.

## Dense-map pusher candidate ordering

The existing spatial grid can gather the same pushable edict from several
cells. `PushGrid_GatherCandidates` must order and deduplicate those candidates
by edict number before `SV_PushMove` processes them: blocked-push rollback is
order-sensitive. Its prior insertion sort cost grows quadratically for a dense
candidate list. Small lists still use insertion sort; larger lists now use an
in-place heapsort with no extra allocation and the same edict order. A pusher
sweep covering more than 4096 grid cells falls back to the canonical edict scan
instead of probing an enormous region. Invalid query bounds take that fallback
too. This removes two potential large-map CPU spikes; it does not establish a
measured frame-time improvement on `mj4m1` or any other map.
