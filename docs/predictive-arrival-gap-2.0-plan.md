# Selected movement: arrival-gap recovery

Status: bounded arrival-gap adapter implemented with passing Linux/software
checks and local Astra Max review/follow-up. The plan preceded production edits
(`0f67b51a`, reviewed contract `3626036b`, terminal correction `351e2dcd`).
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
3. Factor a continuity-only reset from `VR_InputInvalidateMotion` inside
   `vr_input.c`, declare it in `vr_input.h`, and use it from `cl_input.c` at the
   observed producer fence. Keep contact/Gorilla and roomscale reset in their
   existing owner; retain held keyboard and analog levels. Full tracking/focus
   invalidation keeps its neutral-stick behavior. Adapt `cl_parse.c` narrowly:
   accept a validated strictly newer pending epoch without advancing its
   ambiguous completed cursor; use the existing marker epoch latch to prevent
   repeated clearing after same-epoch awaiting-completion metadata. No new
   state/protocol owner or public packet change.
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

Local review: explicit `gpt-6-astra`, effective `max` verified by the reviewer
from model/effort fields only. Read-only review of draft `0f67b51a` and the
baseline production sources; no tests executed by the reviewer. Main verified
the ACK expansion/update order, marker latch, teleport-specific snapshot
commit and retained HMD baseline in the cited source owners.

| Recommendation | Disposition |
| --- | --- |
| A new pending epoch can be invisible after lost completion replies and an ACK distance over 32768. | Adopted: validated strictly newer pending recovery metadata is independent of completion advancement. Keep the old completed cursor until an unambiguous completed marker reply. Extend parser and combined wrap/lost-reply checks. |
| Same-epoch pending -> awaiting-completion -> delayed pending can clear fresh input twice. | Adopted: keep the existing marker epoch latch across awaiting-completion metadata and consult it before producer clearing. |
| GAP overwrites teleport-specific reset semantics. | Adopted: preserve RESET_TELEPORT when publishing a recovery generation; otherwise use GAP. This conservatively repeats a snap once in a later recovery epoch, avoiding a parallel semantic-ACK owner. Check both event orderings. |
| Pending-command clearing leaves the pre-fence roomscale baseline. | Adapted: factor continuity reset within the existing VR input owner; the first fresh sample establishes a baseline, while held analog levels remain usable. Network-only recovery should not invent a requirement to center sticks that held keyboard movement lacks. Full focus/tracking invalidation retains its stronger gating. |
| Reuse the existing four phases, completion tails and idle dispatcher. | Adopted: living RUNNING/AWAIT_COMPLETION timeout fences once; terminal lifecycle remains native; remove obsolete corpse exemption. Do not add idle WALK physics. |
| Expand acceptance to rearming, malformed fenced bodies, death/respawn, epoch/sequence wrap and production tracking sampling. | Adopted: targeted actual-code fixtures plus admitted mixed-peer execution; report remaining evidence limits explicitly. |

No user decision is required for this bounded adapter. The held-stick choice
preserves the requested VR behavior and follows the existing held-key policy.
Implementation/check results will be recorded after the coherent slice.

### Evidence-driven terminal qualification correction

The expanded admitted driver executes actual stock `T_Damage` from the world,
then native gib/death frames. It exposed `invalid owner water level` with
health -99, deadflag 3, MOVETYPE_GIB and SOLID_NOT. Verified source:
`sv_phys.c:SV_CheckWaterTransition` stores the point contents (EMPTY -1 or
SOLID -2) as `waterlevel` outside water; living WALK uses depth 0..3.
Stock `ClientKill` immediately respawns in this cooperative program, so it was
not a valid way to create the intended corpse test.

Before correcting production qualification, the chosen minimal change is to
admit the native range -2..3 only for already classified terminal owners.
Living validation remains 0..3 and non-finite state is still rejected. Native
physics, QC, snapshot and completion ownership remain unchanged. Add an
actual-code guard and rerun the real death/respawn recovery chain. This is a
demonstrated state incompatibility within the planned terminal matrix, not a
reason to replace water categorization or widen living replay.

### Final-review completion correction

The fresh Astra Max implementation pass verified the reuse, tracking helper,
terminal-domain correction and unsigned snapshot packing, but found an ACK
blocker: suppressing completion only on the first newer pending packet lets a
repeat expand completed 65638 to 102 against client completion 100. The chosen
correction freezes completion throughout unresolved selected recovery, using
the existing pending flag and uncompleted marker latch. Recovery metadata is
still accepted; only a completed marker response releases advancement. No new
state owner is needed. Extend the focused parser check with 65638, repeats
before/after marker transmission and awaiting-completion metadata; rerun the
combined matrix and obtain a bounded Astra follow-up before committing code.

## Completed slice and evidence

The server's existing synchronizer now fences a living selected arrival gap
instead of failing/disconnecting. Pause and gap share the existing transient
clear, producer marker and executed-command completion tails. The obsolete
corpse exemption is deleted. Teleport reset semantics survive another fence,
and native corpse water sentinels are qualified without widening living PMove.
The client retains held keyboard/analog input, establishes fresh tracking
continuity, observes recovery metadata independently of ambiguous completion,
and freezes completion for the whole unresolved recovery. Existing state is
reused throughout. A sanitizer-discovered signed snapshot packing shift now
casts to `uint32_t` before shifting; packet layout is unchanged.

| Final review recommendation | Disposition / evidence |
| --- | --- |
| Freeze completion beyond the first newer pending packet; test forward alias 65638. | Adopted with `recovery_unresolved` over existing pending/marker state. Actual parser checks 40000, 65636 and 65638, repeated pending, awaiting-completion and matching executed completion. QC command frame and ACK counters stay unchanged while unresolved. |
| Isolate the marker-latch-only branch after awaiting-completion clears pending. | Adopted: repeated selected-only awaiting-completion metadata retains cursor 100, QC frame 100, zero ACK increments and the marker/reset count. ASan/UBSan check passes. |
| Complete terminal guards and real stock death/respawn/teleport cases. | Adopted: actual admitted mixed world executes both `setpos` orderings, stock world damage, native corpse maintenance, marked respawn and a subsequent quiet living fence. Targeted native guards preserve non-finite/living rejection. |
| Tracking fixture does not execute the full held-stick/contact submission pipeline. | Evidence limit retained explicitly: actual roomscale accumulation and continuity/analog/snap latch behavior are checked; the full OpenXR action/contact pipeline is not inferred from that test. |

Review provenance: all credited local passes were fresh explicitly selected
`gpt-6-astra` at effective `max`, verified from model/effort fields. The final
bounded follow-up found no blocker in the completion correction and no second
completion owner. Main spot-checked its predicate, marker anchoring and server
COMPLETED flag condition, then added the requested latch-only assertion. Static
review did not run or certify the runtime matrix; those checks ran separately.

Checks after coherent implementation:

- Linux `make -C Quake USE_SDL3=1 -j4`: passed.
- Actual admitted mixed-peer recovery matrix: passed normal execution and
  an ASan/UBSan build of its included parser/server-writer sources. It covers
  queued/late actions and roomscale, a marker-only stall, lost commands/replies,
  native modes, repeated forward-alias metadata before physics, full/half-range
  gaps, teleport orderings, real death/respawn, complete message commitment and
  WALK replay. Producer sequences are seeded for long-gap cases; admission is
  real. Other linked engine objects are normally built, not sanitizer-instrumented.
- Native dispatcher/equivalence and terminal qualification guards: passed.
  Unselected mixed native and negotiation default/public/demo checks: passed;
  private transport remains default 1 and selected movement remains default 0.
- Focused pause/marker/receipt and sender checks: passed. Actual ACK parser
  and VR continuity/roomscale checks: passed with ASan/UBSan. Leak scanning is
  disabled due sandbox restrictions; address/UB checks remain enabled.
- Diff whitespace and linked plan references: passed.

The combined fixture explicitly supplies the host/client writer context that
production `SV_SendClientMessages` normally establishes; missing test context
caused its first extended snapshot crash. Player-skin upload remains captured
because dedicated bootstrap models have no renderer textures. Those are test
boundaries, not production workarounds. Connected signon/reliability, headset
input and graphical output are not claimed. Windows/ARM, live device/eye tests
and performance measurement remain deferred as requested.

This closes the bounded normal-gap adapter, not stock default activation or
the migration. Complete the parent's dry/wet/jump, pause/pusher and callback
contracts before enabling ordinary selected movement; AD/cooperative mod
admission and the complete feature matrix remain required.
