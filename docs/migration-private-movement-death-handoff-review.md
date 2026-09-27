# Selected private movement: death and respawn authority

Date: 2026-09-27. Branch: `2.0`. Astra Max senior design review of the
selected per-command movement seam. This is an implementation decision and
qualification plan, not a claim that death/respawn already works. The trial is
still default-off.

## Behavioral reference and architecture choice

Ordinary vkQuake client physics handles WALK, TOSS, BOUNCE, GIB, FLY, NOCLIP
and NONE, chooses dispatch around weapon Think, moves the body on the world
frame, then runs PostThink (`Quake/sv_phys.c`, `SV_Physics_Client`). The selected
private path instead runs up to eight complete command lifecycles per world
frame, with its own queue, credit and completed-command cursor
(`SV_Physics_ClientPrivateWalkTrial`). In stock-style QuakeC, `PlayerDie` can
change the player to TOSS and change solidity, view and velocity. The local
QuakeC source demonstrates that transition; equivalence of every installed
`progs.dat` callback is not yet proven.

| Choice | Reusable code and incompatibility | Decision |
| --- | --- | --- |
| Keep the selected queue as command owner through a terminal transition, reusing the native TOSS/collision helper only at a narrow movement boundary | Preserves per-command pose, contact, QuakeC and completed ACK; native TOSS uses world-frame time and must be reconciled with commandless corpse motion | **Evaluate first**, starting with death after PostThink. Do not copy the general client dispatcher. |
| Defer transfer to native frame owner after a completed command | Reuses ordinary corpse physics, but accepted queue successors are not represented by native latest-command latches; transfer also changes snapshot and replay policy | **Conditional**, only with explicit tail consumption, cleanup ordering, mode publication and respawn recovery. |
| Toggle selection and immediately run ordinary physics | Reruns callbacks already committed by the selected command and can ACK unexecuted successors | **Reject**. |
| Keep disconnect on state change | Contains unqualified behavior but fails normal death/respawn parity | Temporary guard only, not completion. |

The smallest vertical proof is a selected stock-QC player whose PostThink
changes to a terminal state in command `k`, with a distinguishable `k+1`
already accepted and an ordinary public peer observing. A correct result
finishes `k` exactly once, preserves or consumes `k+1` under a stated policy,
never ACKs it merely because it was accepted, and keeps corpse and respawn
behavior consistent with ordinary native physics. After that, cover PreThink,
weapon Think, contact/trigger and commandless transitions; after-contact is
the highest-risk point because the touch list has already begun committing.

## Senior findings and disposition

| Verified finding | Disposition |
| --- | --- |
| All pinned-private snapshots use `private_completed_move` (`Quake/sv_main.c`, `SVFTE_WriteEntitiesToClient`). Native client physics captures `lastmovemessage` at frame entry and later stores it as completed (`Quake/sv_phys.c`, `SV_Physics_Client`). Simply clearing selection can publish the accepted tail as if its individual selected lifecycles ran. | **Adopt**: queue deletion is never simulation completion. Specify exact consumption or cancellation semantics before a native handoff. Keep `retired <= completed <= accepted`. |
| Selected input bypasses native button/impulse latches; nonselected frame-end cleanup clears or restores them (`Quake/sv_user.c`, `SV_ReadPrivateClientMove` and `SV_ReadClientMove`). | **Adopt**: do not stage a successor into native latches before that cleanup. Preserve its duration, actions, pose and contact provenance. |
| Player slots run before other entities in the world loop. A later entity can change a player after the selected command has finished, and snapshot validation currently disconnects that owner (`Quake/sv_phys.c`, `SV_Physics`; `Quake/sv_main.c`, `SV_SendClientDatagram`). | **Adopt**: handle commandless transitions at ingress and publication too. Never manufacture a command completion to leave a state. |
| Maintenance PostThink can change state without reaching the command branch's final eligibility check (`Quake/sv_phys.c`, selected maintenance path). | **Adopt**: use the same transition classification at maintenance completion. |
| The server sends a constant mode epoch and selection chooses owner-reset serialization. The client already parses authority/epoch on equal ACKs and gates replay on coherent snapshots (`Quake/sv_main.c`, `cl_parse.c`, `cl_main.c`). Selection presently occurs only before spawn. | **Adopt**: define transition publication and respawn re-entry together using the existing wire fields. Do not add a second protocol. |
| QSS-M maps dead/TOSS states into PMove, but native TOSS has world-time angular motion, collisions and settling. | **Reject** QSS-M's `PM_DEAD` mapping as proof of vkQuake parity; compare behavior before choosing the narrow adapter. |
| Parser, physics and snapshot perform overlapping eligibility checks. The parser's stale dry-only water check was removed in `fe4eb089`. | **Adopt narrowly**: consolidate gameplay-state classification when implementing terminal states; keep packet malformed-input and snapshot-coherence checks at their existing boundaries. |

The existing GDB-assisted local private/public-peer harness checks basic
movement and attack; it does not assert authority/epoch, death, corpse motion
or respawn. Extend it only when the terminal implementation can be observed
end to end. A packet ACK, isolated fixture or successful build is not by
itself parity proof. Current sandbox execution denies ptrace and UDP sockets,
so the real-map loopback run needs an environment that permits them. Headset,
eye-tracking, Windows and ARM qualification remain separate.

## Native owner reuse checkpoint

`SV_Physics_ClientNativeFrame` now contains the unchanged ordinary client
QuakeC/weapon/movement/PostThink body. Its caller passes the exact input
sequence that may be marked complete; the public/private ordinary path still
passes `lastmovemessage`, preserving its previous behavior. The selected
dispatcher remains a separate gate in `SV_Physics_Client` and still rejects
unsupported state. This extraction is the minimal reuse boundary for a later
terminal-state adapter: do not call the native frame after a selected command
has already run callbacks, and do not pass the selected queue's accepted tail
as the completed sequence. The Linux debug binary builds with the extraction;
no death/respawn behavior is qualified by that build.

## Native-frame adapter follow-up review

Astra Max verified the extracted native body, private parser/retirement, and
client metadata behavior. The native body now returns whether PostThink and
the same owner lifetime completed; its existing ACK assignment uses that same
condition. The ordinary caller ignores the result, preserving its behavior.
The server also withholds prediction permission unless the selected owner is
alive, WALK/SLIDEBOX, dry and outside waterjump/teleport.

| Finding | Disposition |
| --- | --- |
| A fresh-frame terminal owner can reuse `SV_Physics_ClientNativeFrame` with an explicitly staged queue head and its sequence. Invoking the entire native body after selected PreThink, weapon Think, impact or PostThink would repeat callbacks. | **Adopt narrowly.** Reuse only on entry before any selected callback; earlier-phase transitions need continuation at their exact boundary. |
| PostThink death currently fails eligibility before `private_completed_move` advances. | **Adopt.** Give a recognized terminal state a distinct successful command-end outcome, then stop batching; do not silently treat it as invalid input. |
| One terminal queue head per world frame preserves order but can fall behind sustained client arrivals and overflow the eight-command queue. | **Adopt as a blocker for general support.** A one-head path can prove lifecycle correctness, but release behavior needs an explicit bounded-arrival or native-style coalescing contract without lost one-shot actions. |
| A commandless corpse still needs one world-time native frame with held levels and no impulse or room-scale debt. | **Adopt.** Keep the completed ACK unchanged when no head was consumed. |
| Parser and snapshot also reject non-WALK state; authority is currently derived only from selection and the mode epoch is zero. | **Adopt.** Separate queue ownership from published simulation mode, permit only recognized terminal states at each boundary, and publish a mode-epoch transition with replay off; keep respawn native through its current frame. |
| Add another command retirement owner. | **Reject.** Stage the exact head and use `SV_FinishPrivateUsercmds` after a successful frame. |

The next behavioral implementation must cover death already present at frame
entry, commandless corpse motion, and a queued respawn input without an ACK
jump. It must then handle death inside selected callbacks and after later
world entities run. Live stock-QC parity and sustainable queue throughput are
not established by this review or by the current Linux build.
