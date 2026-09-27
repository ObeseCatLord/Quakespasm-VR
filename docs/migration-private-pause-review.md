# Selected private movement across pause: senior review

The `2.0` selected private movement owner currently disconnects a client when
the server pauses: packet admission rejects `sv.paused`, and frame-level input
clearing rejects a selected client even if no packet arrives. Single-player
menu suspension reaches the same clearing path. This is a migration regression,
not an acceptable way to prevent queued actions from executing after resume.

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
| Client ACK16 expansion uses the current outgoing command counter as its high bits. | Account for long suspension where outgoing commands can advance by 65536 or more; otherwise an old completed ACK can be misexpanded. |

The minimal server adapter is a running/suspended/awaiting-completion phase on
the selected owner. The phase observes server pause and single-player menu
suspension before command admission and before each selected physics pass. On
entry it discards queued records once, clears transient input, resets PMove
credit and marks a discontinuity. During suspension, it may validate and
discard wire samples, but it must not execute them or advance completed ACK.
On resume, prediction stays disabled until a fresh command has completed.

The unresolved design point is *freshness*: a late redundant sample is not
necessarily a post-resume sample. The client command producer and existing
protocol must be checked for a sufficient generation/timing fence. If none
exists, add the smallest private-profile fence at that boundary, rather than
another movement queue or a guessed time threshold. ACK expansion has the
same long-pause requirement. This document is a review disposition, not a
claim that the pause fix has been implemented.

Software qualification should cover queued attack/impulse/roomscale/contact
input, no packets while paused, a delay over one second, repeated pause,
single-player menu suspension, delayed redundant packets, a lost first resumed
snapshot, and command-counter wrap. The completed ACK must remain unchanged
until actual command execution. Replay may reopen only from a completed owner
baseline. The user will perform live device testing later.
