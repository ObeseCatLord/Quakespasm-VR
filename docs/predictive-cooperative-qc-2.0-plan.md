# Cooperative QuakeC movement through existing owners

Status: verified implementation plan, before production edits. Extends the
shared-QC checkpoint `be57cdfe`, retaining the complete predictive desktop/VR
goal and all existing native/public behavior. Main owns integration on `2.0`;
the user's modified migration status document stays untouched.

## Outcome and verified boundaries

Mods exporting `SV_RunClientCommand` must receive their actual input globals and
be able to call QSS-M's builtin347, `runstandardplayerphysics(entity)`, to move
their body. Their movement must replace the ordinary engine move, rather than
be followed by another native or selected move. Desktop/public and private VR
players share the same loaded program. Command completion remains owned by the
existing server queue. Arbitrary QC movement is not magically reproducible by
the client; grant existing engine-compatible replay only where its full seed
and implemented solver contract support it, otherwise keep authoritative
correction/native authority explicit.

Verified against the current source:

- `progs.h` declares `SV_RunClientCommand` and the input globals. Loader extension
  resolution exists, and `pr_cmds.c:PR_GetSetInputs` already bridges ordinary
  input. VR metadata remains native-owned.
- `pr_ext.c` lacks builtin347; `pmove_flags` is absent from the extension fields.
  `sv_phys.c` never invokes the cooperative command entry point and currently
  classifies it as native. This means native fallback alone does not execute
  the requested cooperative ABI.
- QSS-M `pr_ext.c:PF_both_pmove` supplies the field/input/solver/materialization
  contract; `sv_user.c:635–703` calls PreThink/Think/hook/PostThink. Its
  receipt-time owner cannot be copied into our world queue without double work.
- `SV_CollectPMovePhysents`, `PMSV_BuildMoveVars`, `PM_PlayerMove`, native
  customphysics, the world Think opportunity, VR contact/weapon scopes and
  private completion/recovery already exist. Reuse them.
- Selected dry commands already have validated time credit and a frame-local
  Think window. Public/native commands instead run on the world interval;
  their old wire format does not supply private accepted msec.

Unknowns: arbitrary cooperative-program gameplay and client-side prediction
semantics, authored repeated builtin calls, custom hull behavior, and interactions
with Gorilla/contact callbacks. These require explicit software proof rather
than another program whitelist. Hardware and Windows/ARM remain deferred.

## Adapter versus replacement

Use QSS-M's builtin semantics at the existing VM registry, adapting its world
collector and movevars calls to the already ported engine helpers. Fully
initialize disposable PMove state, including unsupported/TOSS dispatch, instead
of copying donor stale-state bugs. Add the command hook at the existing native
movement boundary first, preserving PreThink/Think/PostThink, customphysics
precedence, resource lifetime and public desktop behavior. Then extend the
existing selected command owner for cooperative per-command execution; do not
add a second queue, receipt dispatcher, frame scheduler or VM input bridge.

This is an incremental completion path, not a decision that once-world native
cooperative execution finishes modern netcode. A full replacement of server
movement duplicates existing credit, epochs, contact ownership and publication
and has no demonstrated necessity. Reopen if the adapter requires a parallel
callback scheduler or persistent per-program state.

## Stages, ownership and acceptance

1. **Standard server builtin and native hook.** Production write set:
   `Quake/pr_ext.c` (builtin347), `Quake/progs.h` (`pmove_flags`),
   `Quake/sv_phys.c` (existing native callback/input boundary). Reuse current
   `PR_GetSetInputs`, movevars and physent collection. Native private roomscale
   is consumed before QC and must not be applied again by the builtin. Run the
   actual hook after the existing one Think opportunity, then skip native body
   movement. Preserve independent customphysics precedence. Restore temporary
   QC input/clock/context values across nested callbacks. Ordinary programs
   without the hook retain the same native flow.
2. **Selected cooperative commands.** Extend only the existing selected
   execution owner, its typed classifier and permission/stats boundaries
   (`sv_phys.c`, `sv_main.c`, existing replay consumer only if needed). PreThink,
   hook and PostThink receive each accepted command; queued suffixes, positive
   completion and Think opportunity keep existing ownership. The QC builtin
   replaces direct PMove for this branch. Explicitly decide repeated builtin
   duration and VR sample ownership before enabling selection. Quiet
   maintenance cannot invent accepted input or movement credit.
3. **Complete compatible states and replay.** Qualify wet/ladder/customphysics,
   discontinuity, death/respawn, local/load, native-to-command return and public
   coexistence using the same helpers. Keep arbitrary QC custom movement honest
   about prediction. Do not replace this stage with a permanent dry-only gate.

Tests use existing native/session bootstraps, real map collision and actual VM
loading/execution. Add a test-only cooperative QC program generator/fixture;
document assembled/prepared program and captured transport seams. Prove builtin
resolution through the real loader, actual hook input, real body displacement,
one engine move, callback order/clocks, flags/water/contact writes, public/private
coexistence and baseline publication. Negative cases include stale PMove state,
missing optional fields, removed entities, invalid numeric input, callback
relocation and duplicate/zero-duration command behavior. Test after coherent
implementation, with focused diagnostics only for uncertain boundaries.

Tests/docs ownership: a new cooperative fixture/generator, the existing native
fixture Makefile if dependency updates are needed, `tests/README.md`, this plan
and `docs/implementation-plans.md`. No licensed asset copy enters the repository.

## Decisions for local Astra

Current lean: builtin plus native-hook adapter, followed by selected command
integration in the same established architecture. Challenge input restoration,
customphysics precedence, scheduled Think reuse and callback reentrancy. Decide
whether stages1–2 are actually one coupled correctness slice. Prefer deletion
or simpler reuse where it removes a duplicate owner. Direct receipt execution
and silently substituting standard movement for a mod's hook are rejected.
No human taste decision is needed. The review disposition must be committed
before production implementation.
