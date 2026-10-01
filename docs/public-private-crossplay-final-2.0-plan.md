# F01 public desktop / private XR peer

2026-10-01. Existing simultaneous private gameplay now passes. Reuse the same
runner/probes against the current server/XR binary and the already-built
unchanged vkQuake4bc898f2 desktop baseline. That client naturally offers public
FTE/PREDINFO without this project's private profile; no negotiation state is
injected. Preserve isolated profiles, original connect-before-name trigger and
two named peers. Existing local probe already supports QSVR_LOCAL_UPSTREAM1,
private0/prediction0. Disable only its explicitly private-only optional probes;
retain public signon/movement/fire/ACK/native PMove-type assertions. XR keeps
every existing selected prediction/command/presentation assertion.

Test orchestration only: a private scratch copy of the existing runner/probes
changes desktop environment/expected public marker. No repository production
or reference edits, no new interface/renderer/protocol, no builds/deployment.
Keep exact variant script and detailed results beside other private evidence.
Done only if both processes/markers/statuses pass through actual transport and
gameplay. Native baseline renderer limitations do not certify the production
renderer; this is peer compatibility. Windows/hardware/performance remain deferred.
