# Selected private movement beyond dry WALK

This is the follow-up Astra Max senior review of the selected per-command
stock-QuakeC movement owner on `2.0`. It corrects the earlier design brief:
**prediction was already enabled** for an active selected dry client. The server
sets `MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED` in
`Quake/sv_main.c:1884-1894`, and the client publishes successful replay in
`Quake/cl_main.c:1837`. The trial remains default-off. The water adapter now
admits wet states after a dry selection but withholds prediction while wet or
waterjumping. Client replay also stops if a dry snapshot's pending command
touches fluid, including a crossing that ends dry. Real-map behavior remains
unqualified.

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
without command-journal timer propagation. That propagation remains for public
PREDINFO, which does not carry a waterjump timer. A selected snapshot's
completed ACK and timer are one authoritative baseline; an older local replay
value must not override it, including on an equal-ACK update. The focused
client replay fixture checks changed-ACK and equal-ACK private timers, a stale
matching journal cache entry, and unchanged public propagation. A follow-up adapter now admits water
through the selected PMove owner and filters stock QuakeC water drag and ledge
impulse from its PreThink velocity delta. It publishes PMove's waterjump flag
and expiry back to QuakeC while keeping the existing completed-command ACK.
Before permitting wet prediction, this handoff and the actual waterjump
trajectory must be qualified on a real map. The existing `PM_PlayerMove`
categorizes water and handles swimming, friction, and ledge waterjump; it must
remain the movement solver for selected commands. Changes to admission,
snapshot permission, QC velocity ownership, waterjump state, and completed
ACK belong to the same proof.

The [official rerelease QuakeC `client.qc`](https://github.com/id-Software/quake-rerelease-qc/blob/main/quakec/client.qc)
is a useful behavior reference, not proof of byte-for-byte identity with this
trial's pinned `progs.dat`. Its `PlayerPreThink` calls `WaterMove` and
`CheckWaterJump`; `WaterMove` modifies velocity in water and `CheckWaterJump`
can set `FL_WATERJUMP`, upward velocity and a two-second `teleport_time`.
The configured Straight `id1/pak0.pak` contains a `progs.dat` matching all
three trial admission values: 340014 bytes, CRC16 `0x0bf8`, and vkQuake's
XOR-folded MD4 block hash `0xcf69c3e2`. These were independently computed from
the installed PAK entry using the engine's CRC/MD4 conventions. It is a
suitable local test target. Identity with the linked rerelease QuakeC source
is still unproven.
`Quake/pmove.c:1927-2089` also categorizes water, applies friction and detects
ledge waterjumps. QSS-M restores the velocity from before stock PreThink when
its PMove owner runs (`QSS-M/Quake/sv_user.c:673-686`), explicitly avoiding
duplicate QuakeC jump/water velocity edits. A blanket restoration here would
erase deliberate teleporter pauses or waterjump impulses, so the boundary must
be state-specific and checked against the pinned stock QC.

The inherited OpenVR server already has a narrower adapter worth reusing:
`quakespasm-openvr/Quake/sv_phys.c:5645-5750` filters stock QuakeC water
drag, swim-jump and ledge-jump velocity from the PreThink delta while preserving
other QuakeC forces. Its `SV_RunPMoveForEntity` seeds PMove waterjump from
`FL_WATERJUMP` and `teleport_time`, then publishes the resulting timer back to
the edict (`:5777-5845`). It is a behavior reference, not a drop-in server
transplant: that fork runs a different QC cadence and owns movement policy
transitions outside `2.0`'s selected queue. The selected-command boundary now
uses its velocity/timer translation as a reference. A synthetic fixture checks
the velocity handoff; teleporter pause, ledge jump, and QuakeC-authored force
still need the real-map command/ACK proof below.

A follow-up Astra source review found no confirmed P0/P1 in the pinned stock
scope, but identified a conditional callback overwrite: scheduled weapon Think
can change `teleport_time` after PreThink, and restoring the earlier value at
PMove publication would erase that change. The zero-waterjump publication now
preserves a Think-written deadline. Reachability with the pinned stock weapon
Think and real-map timer behavior remain unverified.

The stock QuakeC teleporter sets `teleport_time` and an exit velocity; it does
not set `pausetime`. A later Astra review rejected a proposed `pausetime`
adapter for this slice. The selected PMove command instead carries the active
teleport deadline to the existing air/ground acceleration path, where only
negative forward input is suppressed, matching `SV_AirMove`. Swimming,
waterjump, lateral input, roomscale and hand movement retain their own paths.
While QuakeC holds `fixangle`, selected PMove uses the entity's forced angles
for movement as native `SV_AirMove` does. The original client command remains
available to the QuakeC and weapon callbacks.
The selected snapshot withholds dry replay permission until the deadline
expires, since client replay does not receive that deadline. The review also
confirmed that `SV_WriteDamageToMessage` already emits `svc_setangle` on the
replacement-delta snapshot path.

| Astra decision | Disposition |
| --- | --- |
| Replace broad pause suppression with the native negative-forward rule. | Adopted in the shared PMove air path, enabled only by the selected server command. |
| Do not infer a stock teleport from `pausetime` or import `teleport_time` into the waterjump countdown. | Adopted; no new persistent timer or freeze mode. |
| Withhold selected replay permission while the deadline is active. | Adopted; replay remains authoritative-only for these snapshots. |
| Preserve roomscale and Gorilla collision collection. | Adopted; no pause-based hand exclusion. |
| Verify teleport orientation, exit trajectory and waterjump overlap against ordinary ownership. | Open; the focused PMove fixture checks only backward/sideways acceleration and the forced-angle handoff is source-verified. |

## Waterjump-to-teleport overlap: Astra senior review

Stock QuakeC's waterjump sets `FL_WATERJUMP` and a two-second
`teleport_time`; its teleporter relocates the player and sets a new
0.7-second deadline without clearing that flag. The selected command records
PMove's waterjump countdown before touch callbacks and commits it afterward.
Consequently, a pre-teleport countdown can survive the relocation, and the
next command may shorten or replace the QuakeC deadline. Source ordering
establishes the hazard, but no installed-map trajectory has yet demonstrated
its occurrence or the correct native outcome. Native `SV_WaterJump` also sets
horizontal velocity from `movedir`; its flag suppresses gravity and stepping.
Clearing the timer solely to protect the teleporter's exit velocity would
change those behaviors.

| Review recommendation | Disposition |
| --- | --- |
| Keep the present timer policy pending an ordinary-versus-selected trajectory. | **Adopt.** No speculative timer reset or new owner is added. This overlap remains unqualified. |
| Clear the countdown at the recognized teleport callback and preserve the QC deadline/velocity. | **Defer.** It misses an in-flight PMove result and may diverge from native `movedir`, gravity and stepping behavior. |
| Emulate native `SV_WaterJump` before selected PMove. | **Defer.** Native client consumption precedes PreThink; a helper call at the selected post-PreThink boundary does not reproduce that cadence. |
| Include maintenance, `force_retouch`, and source-trigger cooldown in the proof. | **Adopt.** A commit-only fix would miss callbacks outside a completed selected command; changing the deadline also changes the existing source-suppression key. |

The local non-headset gate uses the same installed `progs.dat` for ordinary
and selected dedicated-server/desktop-loopback runs with controlled
water-ledge and stock-teleporter geometry. First compare one command per world
frame; then selected batching and a no-command maintenance/retouch case.
Require an actual recognized relocation while entering with `FL_WATERJUMP`
and a positive selected countdown, at dry and wet destinations, plus an
ordinary teleport control. Capture origin, velocity, `movedir`, waterlevel,
flag, QC deadline, retained countdown, discontinuity epoch, callback count,
and completed sequence through the first post-teleport steps and expiry.
The chosen adapter must preserve once-only trigger effects and completed-only
ACKs while matching the reference flag/deadline/exit trajectory. If the
comparison favors a teleport cancellation policy, the existing semantic
relocation notification is the narrow boundary, but any in-flight PMove result
must also be invalidated before command completion.
The reviewer was explicitly dispatched as `gpt-6-astra` at `max` effort;
the subagent runtime did not expose effective settings for independent
verification of that selection.

A dry ACK alone cannot guarantee that every pending client command stays dry.
The shared PMove solver now latches fluid contact across all substeps in one
command. Live replay under the selected PMove-engine authority discards its
speculative result and propagation at that crossing; public replay, the
separate QC-command authority, and diagnostic shadow comparison retain their
existing behavior. The production-solver fixture crosses a narrow water region
and finishes dry, and the client fixture checks both journal and unsent-preview
suppression. This closes the local gate leak, not wet prediction parity.

The selected packet reader had one remaining dry-only admission check in
`SV_PrivateWalkTrialStateValid`: it disconnected a selected owner as soon as
`waterlevel` became nonzero, before the wet-capable physics owner could consume
the command. That check has been removed. The physics command boundary still
validates the water level, and the snapshot still withholds wet prediction.
The Linux debug target builds with this correction; real-map continuity and
ACK behavior still need the acceptance proof below.

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
