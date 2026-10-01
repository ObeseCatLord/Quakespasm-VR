# Final fixture follow-up plan

2026-10-01. Test-only continuation of the existing consolidated qualification.
Production feature scope, renderer, protocol and input owners remain unchanged.

## Native metadata permanent-limit case

Main's native debugger trace established that the permanent-limit trial reaches
SV_DropClient, the client parses svc_disconnect, and Host_EndGame calls longjmp
with an unarmed host_abortserver. Ordinary _Host_Frame arms that same boundary.
This is a missing fixture boundary, not evidence requiring a production drop
rewrite. Reuse native host_abortserver/setjmp before driving the connection,
using persistent engine/static state for assertions after longjmp. Select an
explicit permanent one-byte reliable capacity before the native metadata drain;
require native client disconnect and server shutdown, no spawn/signon completion,
and observed native pressure admission. Unexpected host jumps fail all other cases.
Extend the existing runner with one separately named negative case. Retain the
five existing positive cases and real loopback/parser/command paths.

The prior hard500 combined C/Python-line ceiling is reopened BEFORE editing to
580 combined lines, solely for this negative case and readable native jump
handling. No new queue, parser, drop implementation or fake Host_EndGame is allowed.
Stop and report before580 or on any need to change production. Refresh changed
production files in the isolated fixture build and record the exact snapshot.

## Broad controller fixture

The current strict sanitizer build fails linkage at current FBT, calibration,
weapon-menu, presentation and paired-weapon seams. The old command registry also
accepts only vr_turn180 and twelve cvars. Update tests/vr_input_fixture.c and its
README recipe only. Keep all existing assertions and production input/locomotion
owners. Prefer linking reusable pure production helpers or established fixture
seams over implementing replacement policy. Explicitly unavailable optional
subsystems may have narrowly typed spies; document those exclusions, and do not
claim menu/FBT/paired-weapon native integration from such a fixture. Register all
current commands without weakening the turn180-specific assertion. Preserve the
button/context/neutral gating, movement and reentrancy tests.

Bound: stop before250 added/deleted test/README lines or any broad replacement
policy. If the necessary seam surface exceeds that, return the concrete evidence
for a different native-engine test design. No production writes. Strict build and
ASan/UBSan run are the verification targets; sanitizer failure needs diagnosis,
not suppression. Main reviews the complete return and owns integration/receipts.

## Native mixed-network current-frame seam

The strict current-object mixed fixture build fails at three direct writer sites:
SVFTE_WriteStats now takes a deltaframe pointer, and SVFTE_WriteEntitiesToClient
takes the same pointer as its fifth argument. Main inspected the actual native
SV_SendClientDatagram owner: it calls SVFTE_BeginFrame once, then passes that
packet's frame to both writers. Reuse exactly that operation at the two direct
stats/entity snapshot sites and the direct entity-only demo-writer site in
tests/negotiation_native_fixture.c and tests/mixed_native_fixture.c. Preserve the
existing admitted/setup client, captured socket sequence and all assertions.
No synthetic frame allocation, null pointer workaround, ACK/sequence policy or
new snapshot framework. Existing full-datagram Q30 path remains unchanged.

Bound: BEFORE50 changed test lines, precisely those two test files only. Build
using the existing negotiation-native.make and current isolated engine objects.
Run four existing profiles (native, selected+velocityseeds, selected+arrivalgap+
earlypause, defaultselection) in disposable pak0-only roots, with real native
sender/receiver/QC/physics/parser owners and explicit captured-transport/prepared-
signon boundary. Any additional failure needs evidence before a new repair;
do not loosen assertions or change production. Main owns receipts/integration.

### Native respawn settling follow-up

The gap run's original180 world frames at25ms wait4.5 seconds. Main's native
debugger confirms attack received/completed, peer active/RUNNING, deadflag3 and
health-99. Current native co-op policy defaults sv_coop_respawn_delay to10;
SV_CoopRespawnPreparePostThink suppresses the borrowed QC input while that
deadline has not elapsed. Preserve the native delay and every existing assertion.
Derive the test's settling-frame count from max(4.5, configured delay plus50ms),
assert finite reasonable duration/positive frame time, and use that count instead
of180. BEFORE20 changed test lines, mixed_native_fixture.c only; rerun the
previously failing existing gap profile. Do not disable the policy or change
production to satisfy the former wait. This is an existing test timing repair,
not complete co-op delay/inventory/placement qualification.

The corrected deadline reaches actual respawn. A second native trace establishes
that the former shells<25 assertion confuses stock starting ammunition with the
current co-op inventory/world pickups:25 at respawn becomes45 after the attack
window. A diagnostic real impulse2/neutral packet and native world/parser pass
settles that pickup and records46 shells, then the unchanged20 attack frames
consume one shell (45), actor healthy/RUNNING. No gun/physics production failure
is established. BEFORE30 changed respawn-test lines total: select shotgun through
that same native input path, assert actual IT_SHOTGUN and positive ammunition,
capture its current ammunition after native processing, then require an actual
decrease after the existing attack window. Preserve all phase/health/ACK/death
assertions. Do not assign weapon/ammunition, suppress pickups or force policy.
This replaces an obsolete constant with the actual producer/consumer invariant.
