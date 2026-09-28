# Selected movement: arrival-gap recovery

Status: preimplementation draft, awaiting local Astra design disposition.
Baseline: `0b1718a3` on `2.0`. This is the next bounded stage of the
[predictive movement plan](predictive-movement-2.0-plan.md), before ordinary
selected movement is enabled by default.

## Behavior and reference

A temporary absence of movement commands must release stale movement, attack,
impulse and tracking work while keeping the selected player connected. Recovery
must consume freshly produced commands once and reopen prediction only from
completed authoritative state. Desktop and VR use the same recovery policy;
public peers retain vkQuake's native input behavior.

Reuse the selected pause/resume fence, its full-sequence producer marker,
completion cursor, authority snapshot and client replay invalidation. The
behavioral reference is that existing fence plus the QSS-M command-duration
owner: a quiet selected WALK owner retains its current maintenance dispatcher,
rather than acquiring a second world-time movement solver. The existing
one-second arrival threshold stays unchanged. No new setting or wire opcode.

## Verified facts and unknowns

| Evidence | Consequence |
| --- | --- |
| `sv_user.c:SV_ReadPrivateClientMove` and `SV_RunClients` reject a living selected RUNNING owner after a one-second arrival gap. | A normal packet gap is currently treated as trial failure at receipt and during a quiet frame. Both paths need the same recovery boundary. |
| `SV_PrivateSyncPauseState` already clears queued work, last input, credit, buttons, impulse and VR contact state without advancing completed ACK or relocating the player. | Extract this narrow operation and reuse the existing input-phase owner. |
| `SV_HandlePrivateResumeMarker` accepts only a matching generation and full first sequence, rebases 16-bit expansion and enters AWAIT_COMPLETION. | Reuse the marker; delayed pre-fence commands must remain discarded. |
| Selected solver/native completion tails alone change AWAIT_COMPLETION to RUNNING. Snapshots gate prediction on RUNNING. | Marker receipt, queue discard and no-command maintenance must not manufacture completion. |
| The discontinuity epoch is an unsigned short. | Check generation rollover using the existing wire width. |
| `private_move_resume_pending` exists only to exempt post-corpse input from the current fatal-gap rule. | Consider deleting it when recovery replaces that rule; a stale post-respawn exemption must not preserve held input indefinitely. |
| `CL_PrivateMoveResumeObserved` clears button edges, impulse, pending command and duration carry while retaining held keyboard levels. | Keep that producer fence. The separate VR tracking baselines/discontinuity flags require a bounded source check before claiming complete tracking invalidation. |
| `VR_InputInvalidateMotion` resets pending tracking, roomscale baseline, contact/Gorilla continuity and waits for neutral sticks. | Calling it is reusable, but would intentionally gate held analog movement; choose that behavior explicitly rather than assuming it is a harmless memset. |

Unknowns to resolve in the review/implementation: whether AWAIT_COMPLETION must
rearm when its first command never arrives; terminal/respawn interaction;
teleport-reason precedence; and whether resetting pending VR input alone is
sufficient at the producer boundary. Actual connected packet timing and headset
testing remain deferred, not inferred from component fixtures.

## Minimal adapter versus replacement

Preferred: factor the existing transient-input clear, then expand its phase
synchronizer to enter AWAIT_MARKER on a live RUNNING or stalled
AWAIT_COMPLETION owner. Pause still uses SUSPENDED and publishes its new
generation only after resume. AWAIT_MARKER is idempotent even when old packets
continue arriving. Terminal owners keep their existing native death/respawn
dispatcher; returning alive is no longer covered by an indefinite fatal-gap
exemption. Existing finite, framing, QC identity and hull validation remain.

Rejected: disconnecting a valid quiet owner; interpreting the first returning
packet as fresh without a producer fence; adding another queue, timeout state,
ACK protocol or idle physics owner. These either replay stale input or duplicate
working command/completion policy. The expected production scope is one existing
synchronizer and deletion of its obsolete hard-failure paths. Reopen the design
if changes require an additional physics clock or persistent epoch owner.

## Stages, owners and dependencies

1. Record the Astra disposition before production edits. Spot-check its
   load-bearing claims, particularly first-command loss and corpse recovery.
2. Main thread owns `Quake/sv_user.c`: extract transient clear, synchronize
   pause and arrival recovery at the current receipt/frame boundaries, remove
   both fatal gap paths. If the obsolete corpse exemption is removed, update
   `server.h` and its two `sv_phys.c` assignments without changing dispatch.
3. Resolve the producer tracking boundary in `cl_input.c` using an existing
   VR input owner if needed. Avoid reimplementing pending contact/roomscale
   cleanup. Preserve held keyboard behavior and desktop packet bytes.
4. Extend `tests/private_pause_server_fixture.c` for threshold, idempotence,
   no-packet frames, late moves, marker loss/rearming, terminal/respawn,
   teleport and epoch rollover. Extend sender coverage only for a changed
   producer boundary. Use the real admitted mixed-peer driver for captured
   delayed/dropped command and snapshot delivery with QC/world execution,
   complete message parsing and replay.
5. Build and run the bounded suite after the implementation is coherent,
   review the completed slice locally with Astra, record evidence and commit.
   Keep Windows/ARM qualification, live device testing and performance
   measurement deferred as requested. Do not change selection defaults here.

Tests/docs may be delegated with disjoint ownership when the requested coding
route is available. Main integrates all output. The user's dirty
`docs/migration-2.0.md` remains outside the write set.

## Acceptance

- A gap strictly greater than one second fences input once, keeps the selected
  peer active, preserves origin/velocity and jump/waterjump timers at the fence,
  and never advances completed ACK merely by discarding work.
- Old redundant moves cannot revive attack, impulse, roomscale or contact work.
  Repeated snapshots/markers do not clear newly produced input repeatedly or
  manufacture completion. A lost first post-marker command can recover without
  an indefinitely stuck generation.
- A fresh matching marker and actually executed command restore RUNNING,
  authoritative completion and WALK replay. Native NOCLIP/FLY remain native;
  terminal owners retain death/respawn behavior and fresh completion semantics.
- Exercise multiple fences, unsigned-short epoch wrap, full movement-sequence
  wrap, pre-marker teleport, delayed pre-completion metadata and lost replies.
  Preserve genuine malformed/nonfinite rejection and unchanged public peers.
- Report exactly which boundaries are captured/prepared. The combined fixture
  supplies real QC/receipt/physics/snapshot/replay evidence, not connected
  reliability, headset tracking or a benchmark.

## Senior disposition and implementation evidence

Pending; this section must be filled from the verified review and completed
checks rather than retrospectively claiming the draft is implemented.
