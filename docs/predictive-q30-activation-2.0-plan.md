# Normal q30 predictive-session activation

Status: plan before implementation. This implements stages3–4 of the
[transition plan](predictive-q30-transitions-2.0-plan.md), retaining its complete
native-state, admission, publication/parser/replay requirements and the wider
[AD-family goal](predictive-mod-admission-2.0-plan.md).

## Behavior and verified owners

Modern private desktop and VR peers entering the exact installed q30 program
through normal offer/spawn/begin should predict qualified ordinary dry motion.
Startup, authored abilities/holds/camera, liquid and terminal states keep the
existing fresh native input/QC/completion path and legacy replay authority;
ordinary return restores matched QC_COMMAND replay. Public desktop peers remain
native in the same co-op world. Shared weapon/muzzle offsets remain unchanged.
Older peers without Q30_JUMP policy, unknown programs, colliding customstats,
loadgame/local-only and initially wet sessions retain existing native behavior.
Those retained paths do not finish wider-mod/local/load prediction requirements.

Verified current evidence: normal admission in sv_main.c still requires pinned
stock identity, and snapshot replay permission explicitly excludes q30. Typed
BeginState already classifies pre-begin live known-to-QC owners without toggling
spawned. BuildMoveVars already carries live map_jumpheight/maxvelocity under the
negotiated policy; complete stats, parser and shared PMove replay consumer exist.
The exact-q30 callback audits and real1024_tango airborne dry/liquid/retained-head
checks are committed. They are bounded component/static evidence, not an already
connected normal session. See the linked closure/traversal records.

Reuse stock negotiation, per-peer offer/reset, actual Host_Spawn/Begin, queue /
credit, typed current-state predicate, native dispatch, full stats/owner writer,
full parser, command journal and CL_ReplayPlayerMovement. The minimal change is
the existing program/policy admission and replay permission boundary. A second
session harness/transport, queue, ability lifetime, callback scheduler or client
QC interpreter has no demonstrated need. Existing captured transport and prepared
client signon resources must remain explicitly identified in fixture evidence.

## Implementation stages and ownership

1. Extend sv_main.c admission to pinned q30 plus its offered Q30_JUMP policy.
   Preserve initial dry WALK/SLIDEBOX type guards, remote/multislot, robust-elevator, loadgame,
   customphysics/customstat/reference and trusted-input exclusions. Use existing
   BeginState for valid q30 startup/native eligibility, without setting selected
   or spawned temporarily. Existing selected-frame validation remains unchanged.
   Valid changed bounds stay selected-native under the existing classifier;
   replay still requires its stock hull. Add no duplicate bounds policy.
   Avoid a separate initial-state or capability lifetime; validate malformed
   scheduling at the narrow existing owner if evidence requires it.
2. Permit q30 replay only with offered policy, qualified current dry WALK and
   existing completed-frame/input/epoch, finite representable velocity, timer,
   pause, pusher/palm and complete stats fences. Reuse the shared permission
   condition and publisher. A valid native session is not permission to predict
   its abilities. Add no wire layout or renderer/movement solver.
3. Extend tests/mixed_native_fixture.c with a q30-session mode in the existing
   bootstrap/helper owners. Actual client offers, server negotiation, spawn /
   begin, CL_SendMove, SV_ReadClientMessage, world QC/physics/completion, full
   stats/entity serialization/parser and actual journal replay must run.
   Prepared client signon resources/captured delivery and synthetic inputs stay
   explicit; no manual selected/startup lifecycle/ACK/authority assignment may
   manufacture success. Keep public peer native and visible in the same world.
   Verify startup native then ordinary replay, actual dry commands/jump, selected
   session native ability/hold and ordinary return. A staged ability flag may
   qualify state-transition composition but is not an authored pickup/traversal.
4. Update the existing negotiation fixture's injected-writer expectation to the
   new permission contract; keep it distinct from the session proof. Verify
   absent/unknown policy, stat collisions, reconnect and velocity bounds using
   existing fixture boundaries. Consolidate Linux build, q30 normal/session /
   traversal/camera/empty-ammo and stock mixed/negotiation checks after coherent
   implementation. Final local Astra review must challenge admission readiness,
   permission/input order, false-positive tests and unnecessary parallel owners.

Exact production write set: Quake/sv_main.c. A demonstrated missing validator in
the existing sv_phys.c/server.h owner requires recording/recommitting the revised
contract first. Test write set: tests/mixed_native_fixture.c,
tests/negotiation_native_fixture.c and tests/README.md; existing make owners suffice.
Main owns production, integration and documentation. A single requested web agent
may own only the bounded mixed fixture while the unavailable Luna6 route prevents
Luna coding delegation; no overlapping edits or unannounced model substitution.

## Acceptance and limits

Require selected session from real offer/spawn/begin, native startup ACKs followed
by QC_COMMAND/allowed snapshots, complete policy/height/limit inputs, actual
client replay of unacknowledged commands, stable one-completion/queue retirement,
public native peer co-presence, native ability/hold and ordinary replay return.
Preserve stock default/native checks and injected-writer negative boundaries.
Admission cannot be certified by setting selected=true or merely observing a
permission bit. Tests must fail if startup/normal admission is removed.

Remaining real boots/grapple/ladder/map-trigger traversal, all callback effects,
wider AD/Mjolnir/cooperative-QC, local/load integration remain in the parent goal;
do not erase them when this activation slice passes. Hardware/eye, performance
measurement and Windows/ARM qualification remain deferred by the user.
Reopen before a new session/native owner, protocol or callback policy is needed,
or a demonstrated unhandled pre-movement callback invalidates the closure proof.

## Reopened publication contract and Astra disposition

The first final local Astra Max review found a publication bug and two weak
session assertions before activation was committed. Main independently verified
effective `gpt-6-astra` / `max` settings, the snapshot validator's SV_CheckWater
call, overwritten cached fields, single overwrite capture and unused replay
result. Normal-session dry/full-send checks pass, but cannot prove this boundary.

| Finding | Disposition and implementation contract before fixes |
| --- | --- |
| Snapshot's extra strict StateError refreshes q30 water before native input/PreThink, including native-completed frames that currently classify WALK. | **Adopted.** Restrict that additional mutating check to stock; keep q30 on observational FrameStateError already reached through AdmissionFailure. Preserve structural/reference and pusher/palm permission fences. No new water sampler/probe/publication policy is justified. |
| Capture overwrites earlier datagrams. | **Adopted.** Count actual sends and require exactly one for the bounded q30 publication scenario; do not infer full output from the last packet alone. |
| Monotonic ACK and ignored replay result underprove the session. | **Adopted.** Require numerical replay versus subsequent authoritative consumption of the withheld command, exact controlled completion/queue retirement, and horizontal public displacement. |
| Initial hull wording could imply duplicate policy. | **Clarified.** Existing initial type guards and valid body bounds remain; a valid changed hull is native. Replay retains the existing stock-hull owner. |

Extend the test write set with tests/stock_liquid_native_fixture.c, which already
imports both native physics and the mixed publication/session helpers. Its new
q30-publication mode reuses StartLiquidPeers and the one shared BSP finder to
compose cached-dry fields at actual wet geometry after a real admitted native
completion. It must run actual production publication, preserve the cached water
fields, and compare the next native physics/input/completion against the same
accepted-state reference without publication. This adapts the existing q30
stale-dry semantic case without rewiring its movement-only driver to import a
second protocol bootstrap or adding a test API/build owner. Prepared relocation
and bounded player/global/client checkpoints remain explicit, not authored
crossing or restoration of all actors/effect streams.

Production write set remains sv_main.c only. Reuse existing native frame and
snapshot owners, compile under the existing negotiation make target, then run
consolidated software checks and a fresh local Astra fix review. Neither source
analysis nor the existing dry-session pass substitutes for the discriminating
publication case. Remaining parent-goal requirements stay intact.
