# Preserve deliberate paused impulses at the native input boundary

2026-09-30. Before-code plan continuing the source question in
[the reference brief](paused-impulse-2.0-brief.md). Baseline5ab56aa9. Solo
operator, bounded adapter; main retains final architecture. No tests/builds
until all implementation is finished. Main/reference/user-dirty migration
document remain untouched.

## Verified environment

| Fact | Evidence / confidence |
| --- | --- |
| One pending value, latest assignment semantics. | Verified IN_Impulse/cl_input.c:356 and CL_FinishMoveInternal:591–594. The ordinary finalizer consumes before CSQC. |
| Private paused rejection precedes recording; seq0/1 never transmit movement. | Verified CL_SendPrivateMove:1022–1029,1065–1085. Recorded seq>=2 uses the existing redundant command ring. |
| Resume retires old impulse/button/tracking state. | Verified CL_PrivateMoveResumeObserved:962–982. Preserve that existing owner. |
| CSQC filters the projected impulse before private sending. | Verified CL_SendCmd:2727–2745. PF_localcmd adds to Cbuf; no post-filter injection is justified. |
| Local command generation precedes the server frame, then the client parses responses. | Verified host.c:1237–1263. A closed console can leave the server SUSPENDED for this outgoing tick. |
| Native suspension includes server pause and single-player key_dest != key_game. | Verified SV_PrivateSyncPauseState and SV_RunClients. Server selected admission also refuses SUSPENDED/AWAIT_MARKER, independently of cl.paused. |
| Native loopback endpoints are reciprocal driverdata pointers; display strings differ. | Verified net_loop.c:Loop_Connect/Loop_Close. Do not infer identity from localhost/LOCAL strings or assume svs.clients[0]. Native net_bsd.c driver0 is Loopback. |
| Selected readiness has a validated server epoch and positive first-sequence marker. | Verified cl_parse.c accepted source ordering/zero-sequence pause latch and CL_QueuePrivateResumeMarker. Neither prediction permission nor completed recovery is required to send the first fenced command. |
| Execution and effective review routing. | Unverified. Source-only plan; requested Astra xhigh is explicit, effective runtime metadata is unavailable. No runtime, formal routing or final-goal certification. |

## Mostly-worked adapter

Keep in_impulse as the only pending value. Two private input-owner booleans
distinguish deliberate deferred intent and its projection into the current
final command. No second value, generation counter, command queue, ACK retry,
or protocol state machine.

IN_Impulse still assigns the latest value. Every assignment clears the current
projection flag. Tag a nonzero assignment as deferred only for live signed-on
private input during an observed pause/recovery window: cl.paused, selected
resume-pending state, or the matching local server's existing suspension.
Ordinary native/public and unpaused private assignments retain old semantics;
an explicit zero cancels deferred intent. Startup seq0/1 alone does not classify
input as deliberate paused intent.

Add a narrow NET_QSocketGetLoopbackPeer accessor at the native opaque socket
owner: NULL/disconnected/non-loopback sockets return NULL; require a live
reciprocal driver0 peer before returning its borrowed pointer. Never retain it.
At sv_user.c, one read-only local query matches that pointer to an active
private server client. It reads the existing server pause/single-player console
predicate, and selected SUSPENDED/AWAIT_MARKER phase. It never synchronizes,
increments/publishes epochs or changes server state. Factor the current global
suspension predicate for the query and existing SyncPauseState to avoid a new
policy copy. Remote endpoints and unrelated local clients cannot supply state.

Deferred readiness requires live private signon, !cl.paused, seq>=2 and no
matching local suspension. If selected recovery remains pending, require the
existing marker's valid bit, positive first sequence, matching accepted epoch
and current sequence>=that first sequence. An existing sender can establish the
marker on a zero-impulse command; this adapter does not wait for prediction or
recovery completion. Local AWAIT_MARKER may delay one more command until the
ordinary server owner consumes the existing marker. If the server never
observed a brief console opening, its ordinary RUNNING phase allows release
without inventing a new epoch.

CL_FinishMoveInternal projects a deferred impulse before CSQC only when ready.
While blocked, cmd.impulse is zero and in_impulse remains pending; ordinary
button/motion sampling still runs. Previews only observe, never mark projection
or consume. For a final eligible command, mark its projection and retain the
pending value until the existing seq>=2 private recorder accepts that command.
The recorder clears the pending value/provenance only if still projected, even
if CSQC suppressed/replaced cmd.impulse. A later IN_Impulse assignment clears
the projection flag, so recording an earlier command cannot consume that newer
request. No second integer snapshot or serial registry is needed.

CL_PrivateMoveResumeObserved clears ordinary pre-pause input as before, retains
only deliberately deferred intent, and clears its projection flag. Deferred
intent remains pending across further genuine suspension until replaced,
canceled or admitted; that follows the retained latest nonzero impulse policy,
without granting an old movement/attack/tracking sample permission to resume.
Delayed older control events use existing parser/marker ordering and cannot
authorize an unfenced release. Add one pending-input reset helper at the existing
CL_ClearState and CL_Disconnect lifecycle boundaries, clearing the value and
both flags before state teardown. No saved intent crosses maps/connections.

## Alternatives and open review decisions

Deleting resume clearing replays stale input; retaining every sampled impulse
cannot distinguish deliberate new input. Waiting blindly for a newer epoch
strands console intent the server never observed. Reading slot0 or display
addresses confuses endpoint identity. Sending a new reliable impulse command
adds a parallel input protocol. A copied deferred impulse/queue/ACK receipt
duplicates native ownership. Consuming before recording loses rejected work;
injecting after CSQC or retrying filtered zero defeats QC authority.

Main lean is the adapter above. Requested Astra should verify then challenge
whether the two booleans/query/accessor are necessary or can be deleted, whether
the projection invalidation preserves a newer assignment, and whether the
existing positive marker plus local authoritative phase is sufficient without
changing recovery. Report counterexamples rather than adding another epoch
owner. Do not re-audit all replay, physics, QC registry, graphics or VR controls.
No human preference question is needed for this retained input contract.

Expected exact production write set: Quake/cl_input.c, cl_main.c, client.h,
net_main.c, net.h, sv_user.c and server.h; roughly80–130 added adapter/reset lines.
Reopen architecture before materially larger state or an additional owner.
After the disposition, Luna xhigh receives a fully specified coding contract;
main integrates and requests final independent source review of the actual diff.

## End-of-implementation qualification

Use real native console/alias-to-input-to-CSQC-to-recorder-to-server-QC paths
for explicit pause, console-only suspension, unobserved brief console openings,
seq0/1, marker pending/completed and unknown/native/predictive authority,
successive and reordered pause metadata, latest replacement/zero cancellation,
filtered zero/replacement and native redundant delivery. Pre-pause impulses,
button edges, tracking and paused duration must remain canceled. Held controls,
reliable/ACK drainage and unpaused public/private behavior stay native. Map/
disconnect resets and mixed desktop/VR remain required. Software checks run
only after all implementation; user live/performance/Windows checks stay later.
