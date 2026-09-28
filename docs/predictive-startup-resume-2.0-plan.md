# Selected movement: pause before the first movement packet

Status: preimplementation plan. Bounded follow-up to
[arrival-gap/pause recovery](predictive-arrival-gap-2.0-plan.md), before ordinary
selected activation. Work stays on `2.0`; the user-dirty migration document is
outside scope.

## Behavior and verified incompatibility

A newly admitted selected desktop/VR player can pause and resume before sending
its first real movement packet, then move normally. The existing two reserved
startup commands remain suppressed. Recovery keeps the existing reliable marker,
same-datagram redundancy, epoch and completed-command owners.

Verified source: `CL_QueuePrivateResumeMarker` records `cl.movemessages`, which
can still be 0 or 1 at an early pause. `CL_SendPrivateMove` deliberately suppresses
movement sequences 0/1. `SV_HandlePrivateResumeMarker` rejects first sequence <2.
The client nevertheless latches that invalid marker for the epoch, so sequence
2 cannot generate a corrected marker. The server remains fenced indefinitely.
The preceding liquid bootstrap uses normal initial suppression before pausing
and does not prove this early-startup case.

## Minimal adapter and implementation

Normalize the marker's first sequence to `max(2, cl.movemessages)` in its existing
producer helper, using the same value for formatting and the retained latch.
Do not advance the producer cursor, remove startup suppression, relax server
validation, add an epoch or implement another handshake. This is an exact
incompatibility at an existing boundary, not a reason to rewrite recovery.

Main owns `Quake/cl_input.c`, focused `tests/private_send_fixture.c`, the existing
admitted mixed driver and this plan/index. No edits to movement physics, public
packets, protocol layout, graphics, selection defaults or the product branch.
The change is routine under the already Astra-reviewed recovery design; reopen
that design if proof requires a new state or protocol owner.

1. Commit this plan before the production change.
2. Format/store the normalized full sequence in the existing marker producer.
3. Exercise both reserved producer cursors through real send/MSG codecs. Verify
   reliable marker 2, continued suppression of 0/1 and repeated marker preceding
   the first actual movement packet. Existing later-sequence/wrap checks stay.
4. Extend the actual-admission mixed driver with pause/unpause via the real host
   command and `svc_setpause` parser before movement startup. Capture/deliver the
   existing reliable marker, then real produced commands, world/QC completion
   and complete snapshots. Require RUNNING, completed sequence 2 and reopened
   replay permission without manually staging admission/ACK/recovery state.
5. Run the relevant sender/mixed/Linux checks after this coherent implementation,
   record evidence and commit. Actual connected/XR input, Windows/ARM and
   performance measurement stay deferred; this does not complete parent stage 2.

## Acceptance and evidence

Initial pause does not advance movement/ACK. Resume chooses the first legal full
sequence, suppresses reserved commands, completes the real sequence once and
reopens prediction only from the real completed snapshot. Same-epoch reliable
and datagram marker duplicates remain idempotent. The fixture captures transport
and prepares client resources/signon; it is not connected signon/headset proof.

Implementation/check evidence pending.
