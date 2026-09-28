# Cooperative standard physics: command-scoped VR identity

Status: proposed bounded adapter, before production edits. Extends the accepted
cooperative command implementation `999cef74`. Gorilla locomotion and instant
stop are [excluded](migration-scope-decisions.md), not prerequisites for this work.

## Behavior and evidence

Cooperative mods calling `runstandardplayerphysics(self)` must retain ordinary
VR swimming and ladder controls. Desktop players and other QC entities continue
using desktop movement. QC still owns its input edits and the number/duration
of standard physics calls; the existing command owner consumes roomscale once.

Verified on this worktree:

- `sv_phys.c:SV_RunStandardPlayerPhysics` clears a new input command and reads
  only QC globals with `PR_GetSetInputs(false)`. No VR identity survives.
- `pmove.c:PM_IsVRMove`, `PM_WaterUpMove` and `PM_LadderMove` select existing VR
  swim/jump and pitch-independent ladder rules using `pmove.cmd.vr_active`.
- `SV_Physics_ClientNativeFromPhase` already lends an actor/interval context,
  retains the actor, restores nested contexts and executes positive accepted
  heads through the actual QC hook. Public commands have no private VR flag.
- The builtin already borrows/restores all PMove scratch and leaves callbacks'
  final entity writes authoritative. No solver rewrite is needed.

Unknown: authored cooperative mods' full prediction contracts and all ladder
implementations. Prepared QC/input/state tests cannot certify arbitrary mods.

## Minimal adapter and alternatives

Add one immutable VR-identity bit to the existing borrowed context. Capture it
from the current native/accepted cooperative command. After reading QC globals,
copy only that bit to the builtin's command, only for the context's exact actor.
Nested NPC/other-player calls inherit neither VR identity nor tracked movement.

Copying the whole client command would overwrite QC input edits and risks
reapplying roomscale or excluded hand propulsion. A new QC global, handshake,
movement queue or runtime policy duplicates existing identity/ownership without
a demonstrated need. Do not add one. Keep the current QC float duration and
zero-duration no-op behavior; do not manufacture private msec in the builtin.

Expected production scope: about 10 lines in `Quake/sv_phys.c`; no PMove,
protocol, classifier, admission, replay-permission or settings changes. Reopen
the design if the change needs another persistent owner.

## Implementation and software acceptance

1. Commit this plan, obtain local Astra verification of the actor/context and
   nested-call boundaries, and commit the adopted disposition before editing.
2. Implement the borrowed-bit adapter at the existing native context and
   builtin bridge. Retain QC-transformed axes/buttons/time and existing clocks.
3. Extend `tests/cooperative_qc_native_fixture.c` using the shared native liquid
   finder and real BSP collision. Exercise actual producer/receipt/QC/builtin/
   physics/full-snapshot paths for private VR, private desktop and public desktop.
   Compare same-start deep-water motion under jump input; show actual body
   displacement from the existing VR swim policy, not only a copied flag.
4. Exercise one/two standard calls, roomscale-once, nested entity isolation and
   immutable scratch/input restoration. Existing quiet/retained-tail/duplicate
   command checks remain relevant regressions. Qualify prepared `PMF_LADDER`
   control numerically if practical; do not claim an authored ladder traversal.
5. Build Linux and run the coherent focused checks, then final local Astra
   source review. Document evidence/limits and commit explicit paths only.

Tests may add a `PM_PlayerMove` link wrapper solely to observe metadata before
calling the real solver; it is not movement implementation. Test/document write
set: the cooperative fixture, shared fixture Makefile only if required,
`tests/README.md`, this document and `docs/implementation-plans.md`. Licensed
assets stay external. The user's `docs/migration-2.0.md` edit stays untouched.

Compatible cooperative replay, state/local/load admission and the complete
migration remain later required work. User live/device/performance checks and
Windows/ARM verification are deferred.

## Review brief

Solo maintainer; one borrowed bit, no new architecture. Verify the source facts
before critique. Decide whether direct actor identity is sufficient across
nested callbacks, and whether borrowing original VR identity while QC owns
ordinary inputs is the narrowest correct boundary. Challenge any unnecessary
VR fields or settings expansion. Review output should rank concrete issues and
cite file/line evidence; do not revisit the entire renderer/protocol or request
excluded locomotion work. Main can prepare fixture changes while this read-only
review runs, without changing production before disposition.

Environment: this `quakespasm-2.0` worktree only; sibling engines/assets are
read-only references, captured transport/native VM fixtures are available,
Linux SDL3 is the current builder, and Windows/ARM/live hardware are deferred.
