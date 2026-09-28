# Selected prediction: unpause before recovery metadata

Status: planned before production edits in `e72aa276`, implemented with bounded
software acceptance; final Astra disposition below. Prompted by the live-liquid
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
The parser rejects same/older selected generations and requires an actual newer
pending fence while this zero-sequence pause latch is active. A delayed
pre-pause semantic-relocation snapshot may itself have a newer epoch, so a
completed flag in a merely newer snapshot is insufficient. A newer actual
pending epoch clears the latch through
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
   Invoke actual replay before a new epoch and after the old snapshot. Also
   withhold a newer pre-pause snapshot from real setpos relocation: it must not
   be mistaken for the server's resume fence. Print
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

## Implementation and software evidence

Main reproduced the initial issue before production edits: with actual host
pause/unpause services and withheld snapshots, both ordinary wet movement and
an active e1m2 ledge reopened live replay and accepted the old snapshot. After
the correction, both log `reopened=0 stale_reopened=0`; a marked command after
the true new pending fence completes and restores actual replay. The plain wet
case also withholds a newer pre-pause snapshot from real `setpos`/`noclip 0`
relocation. Merely newer completed metadata is rejected, preventing it from
being mistaken for the current resume fence.

No new client/server field, protocol, timer reset or epoch owner was added.
The existing tuple's zero first sequence explicitly means no marker has been
produced for the latched paused generation. The existing sender cannot emit
that old-generation marker, and the existing newer pending observation resets
the tuple before the normal legal first sequence is chosen. ACK/world timers
stay unchanged through suspension; active ledge recovery preserves positive T.

Linux SDL3 `-Werror`, actual mixed early-pause/arrival-gap/native/traffic-loss/
wrap/death/respawn checks, the full water/slime/lava/generated-VR/10/25/100-ms
transit matrix, and partial combined ASan/UBSan drowning/e1m2 ledge-recovery
checks pass. Focused move-ACK ASan/UBSan covers same-epoch suspended/completed
and newer completed rejection, fresh pending acceptance and epoch wrap.
Combined sources are instrumented, other engine objects normal; leak detection
is disabled. This is not whole-engine sanitizer or connected/headset evidence.

## Architecture reopened: causal reliable pause context

The correction review found a source-derived liveness incompatibility: an E+1
pending snapshot may precede delayed Boolean pause/unpause services. Latching
the latest client epoch then waits for E+2, which the server never produces
while awaiting E+1's marker. Completed E+1 metadata before the old services is
indistinguishable from a genuinely new pause at E+1 using Boolean bytes alone.
Another client Boolean cannot recover the missing source information. The
earlier tuple-only design is superseded before production integration.

Chosen minimum adapter, following Astra's first-ranked option: reuse the
existing standalone `QSVR_SVC_MOVEACK` encoding immediately before
`svc_setpause` in each selected client's reliable buffer. Synchronize that
client through the existing pause/fence owner at the actual host toggle before
writing its epoch; do not sample later or globally broadcast one client's
generation. Its control ACK has selected/pending/discontinuity metadata and
UNKNOWN authority, never prediction permission or a manufactured owner state.
Public/native recipients keep the ordinary Boolean pause service.

Expose the validated raw source epoch to the immediately adjacent pause
handler even when ordinary ACK acceptance rejects a stale body. Pairing
context is local to the parsed message, reset at intervening services, and is
never a new persistent policy owner. An old pause preserves newer accepted
pending/completed metadata; a current/source-ahead pause invalidates replay and
latches that source generation. The existing zero-sequence tuple waits for a
newer pending epoch relative to its source, and the producer cannot emit a
marker from an unobserved generation. Normal snapshot/marker/completion owners
remain unchanged. Demo parsing validates/consumes the pair without live fences.

Rejected alternatives: a new versioned pause opcode if ACK reuse proves
misleading; reliable-send-first cannot enforce cross-channel arrival ordering;
waiting for transport ACK would introduce a broader scheduler; preserving
pending alone does not solve completion-before-services. No new protocol body,
capability, client field, timer reset, ACK owner or packet transport is needed.

Additional main ownership: narrow `host_cmd.c` notification caller, `sv_user.c`
existing pause synchronization plus per-client writer, its `server.h` declaration,
`cl_parse.c` validated raw output/message-local pairing, `cl_input.c` zero-marker
guard, mixed helper/captured driver and focused ACK fixtures. Expected change
is one reliable notification adapter and small parser/producer boundaries;
reopen again if it grows into an independent recovery state machine.

Qualification before commit: real pending snapshot before delayed services,
completed snapshot before delayed services, a genuinely new pause after
completion, rapid pause/unpause with no intermediate run-client tick, multiple
clients' distinct epochs, and wrap. All must complete the ordinary marker and
full snapshot without an extra pause. Retain the original lost-snapshot/newer
pre-pause relocation cases and public pause bytes. Final Astra review checks
the causal adapter and the unchanged movement owner architecture.
