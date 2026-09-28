# Selected prediction: unpause before recovery metadata

Status: planned before production edits, prompted by the final live-liquid
Astra Max review. Bounded follow-up to the existing arrival-gap/startup-resume
plans. The product branch and user-dirty migration document remain untouched.

## Behavior and verified incompatibility

For an already observed selected owner, receiving a real pause must prevent
replay of discarded pre-pause commands until a newer server recovery epoch
completes. Unpause alone, lost suspended/pending snapshots and a delayed
pre-pause snapshot must not reopen replay. Public/native and demo behavior
remain on their existing owners.

Verified source: `svc_setpause` currently only assigns `cl.paused`. Unpause
removes the replay entry guard while leaving permission and the accepted
baseline intact. `StartupPauseCommand` always supplies a snapshot after pause
and unpause, hiding that interval. The client already has one resume-pending
flag, a server epoch and an epoch/first-sequence marker latch; sender/parser
use those fields together. Server resume increments the existing epoch.

## Adapter comparison and scope

Use the existing client recovery tuple to latch the paused generation:
resume pending, marker epoch valid for the current accepted epoch, but first
sequence zero (no marker produced). This deliberately fences that epoch and
prevents the sender from manufacturing a marker for an unobserved new epoch.
Invalidate its accepted replay snapshot/permission at the pause service.
The parser rejects same/older selected generations while this zero-sequence
pause latch is active. A newer actual pending epoch clears the latch through
the existing observation path, then the normal producer chooses its legal
first sequence and completion reopens replay. Server-owned epochs are never
invented/incremented by the client.

Alternative: a new persistent waiting-for-epoch Boolean would distinguish the
same state, but duplicates the existing epoch/first-sequence tuple. Reject it
unless review demonstrates an ambiguity in the existing fields. A new protocol,
ACK owner, state machine or timer reset is unnecessary. Reopen this decision if
the tuple cannot express the fence without confusing existing producer/parser
rules. This adds no new physics/graphics behavior or selected activation.

Main owns `Quake/cl_parse.c`, `tests/stock_liquid_transit_fixture.c`, the focused
move-ACK fixture as needed, and this plan/index. Existing `cl_input.c` marker
producer stays reused; no new first-sequence or world-state owner.

## Stages and acceptance

1. Commit this plan. In the admitted real-stock driver, capture a completed
   pre-pause snapshot and produce a pending wet command. Deliver real host
   pause and unpause services while withholding suspended/pending snapshots.
   Invoke actual replay before a new epoch and after the old snapshot. Print
   the pre-fix reproduction, then require denial after the fix.
2. Latch the observed generation and invalidate permission/baseline at the
   existing pause service; reject its delayed metadata before ACK commitment.
   Keep the existing completed-generation stale-metadata guard.
3. Verify no old-epoch marker is produced while awaiting metadata. Deliver the
   actual new pending snapshot, send/deliver the ordinary marker/command,
   complete actual QC/world work and commit a full snapshot. Replay reopens
   only then. Cover plain wet movement and positive owned ledge timer, keeping
   completed ACK/world state unchanged through suspension.
4. Run relevant Linux/mixed/ACK and partial sanitizer checks after the coherent
   correction. Fresh local Astra Max reviews the tuple/ordering correction;
   main spot-checks and records its disposition before commit.

Captured transport, prepared starts/resources/input and controlled packet
ordering are explicit software seams. No connected/headset, Windows/ARM or
performance claims. This closes a demonstrated boundary, not full-domain
selected movement or the parent migration goal.
