# Selected private movement beyond dry WALK

This is the follow-up Astra Max senior review of the selected per-command
stock-QuakeC movement owner on `2.0`. It corrects the earlier design brief:
**prediction is already enabled** for an active selected client. The server
sets `MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED` in
`Quake/sv_main.c:1884-1894`, and the client publishes successful replay in
`Quake/cl_main.c:1837`. The trial remains default-off and dry-only. This review
does not qualify wet movement or unrestricted private prediction.

## Verified architecture and decision

| Senior finding | Disposition |
| --- | --- |
| A selected owner executes up to eight queued commands per server frame; ordinary `SV_Physics_Client` instead uses latest-command frame physics and publishes `lastmovemessage`. Switching to it after movement or a callback has run can replay actions or ACK later queued input. | **Adopt.** Keep the selected queue, credit, per-command QuakeC lifecycle, and completed-command ACK as the single owner. Do not add a generic fallback mode or run ordinary physics after a selected-command failure. See `Quake/sv_phys.c:7489-7616, 7908-8074`. |
| Movement reaches the edict before impacts and triggers. Eligibility can then fail before `PlayerPostThink` and ACK completion. The current owner disconnects; a mode epoch cannot roll back an impact, trigger, or weapon action. | **Adopt.** A supported state transition must finish its current command exactly once. The ACK advances only after all command callbacks complete. Do not replace a post-commit disconnect with a retry. |
| Wet states are rejected both in physics (`SV_PrivateWalkTrialStateError`) and the snapshot preflight (`SV_SendClientDatagram`). | **Adopt.** Change these two gates in one vertical slice. Keep prediction permission off for the expanded wet-state experiment until authoritative seed and replay parity are established; retain dry permission only if the server can prove the snapshot is dry and has a coherent completed ACK. |
| Existing authority, mode epoch and discontinuity metadata already reach the client, including equal-ACK updates. Wet/dry transitions that keep the same per-command simulation owner need no additional mode state machine. | **Adopt.** Reuse metadata if authority truly changes later; do not invent an epoch merely because the player enters water. See `Quake/sv_main.c:1892-1895`, `Quake/cl_parse.c:3007-3025`, `Quake/cl_main.c:1195-1210`. |

The next implementation slice is **stock-QuakeC water in the existing selected
per-command owner**, preserving the one-command lifecycle and VR weapon pose.
At review time the server seeded `pmove.waterjumptime = 0` and exported only
jump debounce (`STAT_PRIVATE_JUMP_SECS`). The current adapter now retains and
exports the authoritative waterjump timer as `STAT_PRIVATE_WATERJUMP_SECS`,
requires its receipt in selected snapshots, and seeds client replay from it
before command-journal propagation. The dry-only gate still prevents claiming
water support. Before permitting wet prediction, QuakeC/PMove water ownership
and the actual waterjump trajectory must be qualified. The existing `PM_PlayerMove`
categorizes water and handles swimming, friction, and ledge waterjump; it must
remain the movement solver for selected commands. Changes to admission,
snapshot permission, QC velocity ownership, waterjump state, and completed
ACK belong to the same proof.

The [official rerelease QuakeC `client.qc`](https://github.com/id-Software/quake-rerelease-qc/blob/main/quakec/client.qc)
is a useful behavior reference, not proof of byte-for-byte identity with this
trial's pinned `progs.dat`. Its `PlayerPreThink` calls `WaterMove` and
`CheckWaterJump`; `WaterMove` modifies velocity in water and `CheckWaterJump`
can set `FL_WATERJUMP`, upward velocity and a two-second `teleport_time`.
`Quake/pmove.c:1927-2089` also categorizes water, applies friction and detects
ledge waterjumps. QSS-M restores the velocity from before stock PreThink when
its PMove owner runs (`QSS-M/Quake/sv_user.c:673-686`), explicitly avoiding
duplicate QuakeC jump/water velocity edits. A blanket restoration here would
erase deliberate teleporter pauses or waterjump impulses, so the boundary must
be state-specific and checked against the pinned stock QC.

## Acceptance proof for the water slice

Run one selected remote stock-QC client and one public-protocol observer on a
real map, dry → shallow/deep water → dry. Include batched commands, a brief
input gap, jump, a weapon action with observable ammo/damage, and ledge
waterjump. Require continuous movement without disconnect or debt, once-only
QC/trigger/weapon effects, completed-only ACKs, and unaffected public
observation. Keep expanded-state prediction permission off until authoritative
waterjump seeding and earlier-ACK replay comparison pass. User headset and
cross-platform validation remain separate. Death, pusher rides, non-WALK
movement, custom physics, and other mod policy remain open; none is made safe
by merely deleting its eligibility check.
