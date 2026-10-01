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
