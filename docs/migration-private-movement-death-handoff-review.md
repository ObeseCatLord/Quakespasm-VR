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
| One terminal queue head per world frame preserves order but can fall behind sustained client arrivals and overflow the 32-record/500 ms queue. | **Adopt as a blocker for general support.** A one-head path can prove lifecycle correctness, but release behavior needs an explicit bounded-arrival or native-style coalescing contract without lost one-shot actions. |
| A commandless corpse still needs one world-time native frame with held levels and no impulse or room-scale debt. | **Adopt.** Keep the completed ACK unchanged when no head was consumed. |
| Parser and snapshot also reject non-WALK state; authority is currently derived only from selection and the mode epoch is zero. | **Adopt.** Separate queue ownership from published simulation mode, permit only recognized terminal states at each boundary, and publish a mode-epoch transition with replay off; keep respawn native through its current frame. |
| Add another command retirement owner. | **Reject.** Stage the exact head and use `SV_FinishPrivateUsercmds` after a successful frame. |

The next behavioral implementation must cover death already present at frame
entry, commandless corpse motion, and a queued respawn input without an ACK
jump. It must then handle death inside selected callbacks and after later
world entities run. Live stock-QC parity and sustainable queue throughput are
not established by this review or by the current Linux build.

## Terminal handoff implementation checkpoint

The selected PostThink command now completes its own ACK when the owner enters
a recognized dead state, then stops batching. At the next fresh physics frame,
the existing native dispatcher handles corpse motion and possible respawn.
Accepted dead-state samples are staged with the native private parser's latest
levels, latched attack/jump, and last nonzero impulse; ordered physical-contact
and Gorilla samples are invalidated while the owner is still dead. The native
dispatcher advances `private_completed_move` only after its owner lifetime and
PostThink complete. A commandless corpse frame keeps the ACK unchanged. Parser
and snapshot admission recognize this terminal state, and snapshots publish
legacy authority with a mode-epoch change and prediction off until the next
selected frame. The Linux debug build passes; a live death/respawn proof has not
run.

At this checkpoint, death during selected PreThink, scheduled weapon
Think, movement impact, physical contact, or trigger callbacks still reaches a
state-error disconnect. Those phases need exact continuation after the callback
already run, without restarting the native body. Respawn after a long command
silence, paused selected sessions, and command coalescing behavior still need
qualification. The default-off trial must not be described as full death
parity on the strength of this checkpoint.

## Astra follow-up on the checkpoint

Astra reviewed commit `01b78180` against the current server owners and ranked
three findings. The fixes below are source-reviewed and Linux-build checked;
the in-game lifecycle still lacks an end-to-end run.

| Finding | Disposition |
| --- | --- |
| PreThink, weapon Think, and movement/contact deaths still disconnect at the reviewed checkpoint. | **Adopt, partial.** The late contact boundary now reaches the selected PostThink/completion tail. Earlier callbacks still need their exact remaining phases; never restart the entire native frame after QC has run. |
| The corpse idle-timeout exemption also changed ordinary pinned clients. | **Adopt, fixed.** Require selected trial ownership for the exemption. Ordinary pinned clients retain their original input clearing. |
| Early terminal queue validation could call `SV_DropClient` with another `host_client` global. | **Adopt, fixed.** The explicit-client drop helper now binds and restores `host_client` and `sv_player`. |

The native terminal frame now also grants one fresh selected command after a
long corpse interval: `private_move_resume_pending` survives commandless
maintenance and clears on the first accepted living command. This closes the
obvious idle-respawn disconnect in the source path, but packet-level behavior
remains unverified. Coalesced attack/jump/impulse behavior likewise still needs
the planned live comparison with ordinary native physics.

The late selected movement boundary now accepts a recognized terminal state
after impact, trigger, and physical-contact processing, then runs the existing
selected PostThink and completion tail exactly once. A death inside an earlier
PreThink or scheduled weapon Think still needs a shared remaining-phase
continuation. Death from a callback that invalidates the owner remains a
disconnect. The Linux debug build passes; contact-death behavior has not yet
been observed on a live stock map.

## Early callback continuation checkpoint

The native client frame now has three entry phases: fresh, after an already-run
PreThink, and after an already-run scheduled weapon Think. The ordinary caller
uses the fresh phase, preserving its callback order. When a selected command
dies in either earlier callback, the shared native remainder runs the still-due
weapon/movement/PostThink phases with the world clock. The current command's
dead contact/Gorilla samples are fenced first. The native remainder is passed
the *previous* completed ACK; the selected completion tail advances the killed
command only after that remainder succeeds. Maintenance deaths run the same
remaining phases without manufacturing an ACK. If the remainder respawns the
player, selected batching stops until the next world frame.

This source-level checkpoint builds on Linux. The native move-frame capture
occurs when the remainder starts, after the selected callback; selected WALK
admission rejects an existing moving-pusher owner, but same-callback support
changes and exact stock-QC behavior have not been exercised live. The senior
review of this phase adapter is recorded below. Physical contact, death and respawn
packet sequences still need the end-to-end comparison before enabling the
trial by default.

## Early phase senior-review disposition

Astra found one concrete source-level correctness risk: the selected command
removed stock-QC water drag and dry jump velocity *before* scheduled weapon
Think so PMove could replace them. If that Think killed the player, the native
remainder inherited velocity that PMove would no longer consume. **Adopted:**
the PMove-only reconciliation now runs after weapon Think survives. Think sees
native QC velocity, and a Think that writes velocity keeps its own result;
the selected path does not overwrite it with a jump rewind. The ordinary
fresh-frame native path remains the same. The Linux debug build passes.

The review found no additional concrete callback duplication or ACK/contact
ordering defect in the phase adapter. The after-Think velocity-write policy,
corpse momentum, callback counts, and release/press respawn still require a
live stock-QC comparison; source review and compilation do not prove parity.
