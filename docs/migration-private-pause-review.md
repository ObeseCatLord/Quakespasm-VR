# Selected private movement across pause: senior review

Before the pause adapter, the `2.0` selected private movement owner disconnected
a client when the server paused: packet admission rejected `sv.paused`, and
frame-level input clearing rejected a selected client even if no packet arrived.
The selected stock-QC trial currently requires multiple server slots, so its
single-player menu predicate is defensive rather than a qualified selected
gameplay path.

The local Astra `gpt-6-astra`/`max` review checked `sv_user.c`, `sv_phys.c`,
`sv_main.c`, `cl_parse.c`, `cl_main.c`, and the production-sender fixture. It
recommended preserving the existing selected command owner, queue, and replay
machinery, with an explicit suspension phase. No new movement owner is needed.

| Finding | Disposition |
| --- | --- |
| A paused selected client is dropped in both packet and no-packet paths. | Fix at the existing selected input admission and frame owners. Suspension must retain the connection. |
| Discarding queued commands alone leaves held buttons, impulse, roomscale/contact continuity, last command and PMove credit. | Clear transient input state on suspension entry, while preserving authoritative physical state and gameplay timers. Discard without advancing the completed ACK. |
| `private_move_resume_pending` clears on command acceptance. | Keep its terminal/respawn purpose. A separate suspension state must hold prediction closed until a resumed command completes its callbacks. |
| Snapshot publication currently reopens prediction whenever the server is unpaused and the ordinary owner predicate passes. | Require resumed-command completion before publishing prediction permission and a coherent completed baseline. |
| `GAP` does not purge client command history; replay can begin after the unchanged completed ACK. | Do not use GAP or the discard cursor alone as a freshness fence. |
| The sender repeats up to three commands. A packet arriving after resume may include unseen pre-resume commands. | Do not equate receive time with production time. Establish a producer/resume fence before claiming that paused attack, impulse or movement cannot execute after resume. |
| Client ACK16 expansion used the current outgoing command counter as its high bits. | Fixed in `cl_parse.c`: expand against the last completed ACK and reject a future ACK. A focused fixture covers an unchanged ACK after 65536 generated commands. This does not solve input admission across pause. |

## Implementation and second senior review

The adapter now has running, suspended, awaiting-marker and
awaiting-completion phases on the existing selected owner. Pause entry discards
queued records, clears transient buttons, impulse, roomscale, contact and held
command state, and resets PMove credit without changing the completed ACK or
physical player state. On resume the server increments its discontinuity epoch
with `GAP` and publishes `RESUME_PENDING`. A full-sequence `qsvr_resume`
marker identifies the next command generated after the client learned of
resume. At the ACK boundary the client
clears pre-observation key edges, impulse and accumulated movement/contact
input while retaining held keys. It queues the marker reliably if there is
space and repeats it before moves in each unreliable datagram until completion.
The inline copy admits fresh post-observation input even under reliable-channel
backpressure. This preserves the pinned movement record layout. The server
discards pre-marker commands and keeps prediction
closed until a post-marker command completes its callbacks.

The second Astra review found four integration hazards. Its original patch was
not accepted as-is; these corrections are in the same implementation slice:

| Finding | Disposition |
| --- | --- |
| Terminal players publish `LEGACY_FRAME`, so an engine-authority-only marker would deadlock respawn. | Added an explicit selected-owner ACK flag independent of movement authority. The terminal/native completion path also releases the fence after successful completion. |
| A prepared command can contain input sampled before the resume ACK. | Clear producer key edges, impulse and accumulated movement/contact input when a new selected `RESUME_PENDING` epoch is parsed; fresh input sampled afterward may use the first marked command. |
| A full-sequence server marker can jump more than 32768 beyond the client's previous completed ACK. | Added a resume-completed ACK flag. Before completion, expand against the old completion; after completion, expand against the marker's full first sequence. Reject delayed pre-completion metadata for that generation. |
| Clearing the held-command validity indirectly resets jump and waterjump timers. | Suppress initial timer reset during suspension/awaiting phases, while still invalidating the held command. |
| A relocation while awaiting the marker replaces GAP with RESET_TELEPORT. | Keep `RESET_TELEPORT` for client snap and roomscale invalidation. A separate `RESUME_PENDING` ACK flag continues the handshake at the new epoch. The server and ACK fixtures cover both paths. |

The source-level client sender, ACK parser and server admission fixtures pass,
including a 65536-command ACK gap, marker ordering, repeated pause, and terminal
marker production. The Linux `vkquake` build passes. These checks do not yet
prove end-to-end QuakeC callbacks, packet loss/reordering, or visual/device
behavior. They are an implementation checkpoint, not release qualification.
The final narrow Astra Ultra review verified the `RESUME_PENDING`/teleport
interaction and found no remaining blocker in that path; it did not rerun the
checks or qualify the complete multiplayer session.

Remaining software qualification should cover actual client/server packet
delivery and QuakeC callbacks with live and dead selected players, queued
attack/impulse/roomscale/contact input, no packets during a pause over one
second, repeated pause, a teleport before the marker, delayed redundant
packets, a lost first resumed snapshot, full sequence wrap, and unchanged
jump/waterjump timers. The completed
ACK must remain unchanged until execution; replay may reopen only from a
completed owner baseline. The user will perform live device testing later.
