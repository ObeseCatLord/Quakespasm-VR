# Cooperative QuakeC movement through existing owners

Status: stages1–2 and the [VR identity adapter](predictive-cooperative-vr-input-2.0-plan.md)
are implemented with bounded software checks; complete states/replay in stage3
remain open. [Current scope](migration-scope-decisions.md) excludes Gorilla
locomotion and instant stop. Extends the
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
   `Quake/sv_phys.c` (existing native callback/input boundary), `Quake/sv_user.c`
   (native-force gate and decoded public metadata), `Quake/pmove.h` (one
   engine-facing builtin adapter declaration). Reuse current
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

## Astra design disposition

Local Astra Max reviewed the verified source brief; main independently verified
effective `gpt-6-astra`/`max` and spot-checked the public parser, native Think/body
order, impact owner and donor TOSS fallthrough. No implementation/runtime review
is implied by this design pass.

| Recommendation | Main disposition |
| --- | --- |
| Native-world hook and builtin can precede selected hook execution. | Adopted as an incremental stage, with native classification/legacy authority retained. This does not finish cooperative prediction. |
| Native input can accelerate before the new hook. | Adopted: preserve angle/recoil updates, then bypass native forces and deferred Gorilla movement for the cooperative owner. Capture actual public timestamp/angles/buttons/impulse in the existing command record, retaining old edict impulse latching. |
| Standard physics callbacks can recurse and overwrite PMove scratch. | Adopted: reuse the existing saved-PMove pattern. Materialize results before callbacks, retain the contact entities before the first callback, stop stale contacts after removal/relocation, and restore enclosing scratch/context. Keep impacts in sv_phys.c through its existing helper. |
| Input round-trip restoration loses bits/vector components. | Adopted: save exposed input globals as raw words, including full vectors and integer aliases. Restore transient globals/VM self-other/clocks/native context on normal exits; preserve authored entity state. Validate numeric globals before the existing bridge converts integer fields. |
| Standard builtin plus native final relink can duplicate triggers. | Adopted: the hook owns its movement/trigger dispatch, with a non-touching native final relink. A frame-local standard-call marker also avoids repeating builtin triggers when independent customphysics uses it; customphysics precedence remains unchanged. |
| Donor TOSS dispatch was described incorrectly. | Corrected: its commented-out assignment/break falls through to PM_DEAD. Use explicit initialized cases while preserving that actual behavior. |
| Bound each standard invocation by the engine-owned interval. | Adapted: finite0..B per call, zero a no-op, where B is the captured world interval for stage1 (accepted command duration in stage2). Repeated authored/subdivided calls remain possible and never independently retire/debit commands. This bound is an explicit adapter restriction, not donor parity. |

The standard-call marker/input snapshot/interval are borrowed stack scopes at
the current native owner, with restoration across nesting, not persistent
client state or a scheduler. Main will implement and consolidate the prepared
loader/public/private/world/send proof, then request a bounded source recheck.
Gorilla mapping, selected per-command callbacks, arbitrary QC prediction and
broader mod/local/load compatibility remain mandatory later stages.

## Stage1 implementation checkpoint

Builtin347 now resolves through the normal server loader. The native owner
publishes actual decoded inputs, calls the cooperative hook after its existing
Think opportunity, and lets QC own movement without an additional native move.
Public desktop and private VR players execute the same program. Ordinary
programs and independent customphysics retain their existing owners. The
builtin uses a fresh shared solver/collector state, materializes results before
contacts and restores borrowed scratch/context. Input globals restore raw bits,
including all three vector words and integer aliases.

The first runtime check exposed float/double interval rounding: a QC25ms float
exceeded the corresponding double world interval. The bound now compares at QC
float precision while retaining the engine-owned interval. Zero time remains a
no-op; two authored12.5ms calls intentionally need not match one25ms call under
the existing NQ friction order.

Local Astra Max performed bounded production-source reviews, with effective
`gpt-6-astra`/`max` settings verified from local metadata. Main checked the
load-bearing findings and owns fixture review and execution.

| Implementation finding | Disposition |
| --- | --- |
| A standard-call marker could suppress triggers in a later movement phase. | Fixed: reset/capture only around replacement customphysics; suppress its epilogue only at the last actually dispatched position. Ordinary Think/PreThink calls cannot suppress later native links. |
| A non-solid builtin call could mark a trigger dispatch that never happened. | Fixed: record the marker inside the solid dispatch immediately before the actual touching link. The final Astra recheck found no remaining issue in this delta. |
| Public/native impulse conversions preceded validation. | Fixed: retain the raw public wire impulse; validate the native latched QC impulse before converting it for input projection. |
| Airborne results retained an old ground entity. | Fixed: clear it when the solver clears on-ground. |
| Gorilla metadata is absent from this standard solver adapter. | Still open: stage1 gates native forces and does not qualify cooperative Gorilla locomotion. Complete it at the selected command/sample boundary rather than claim inherited parity. |

The final Linux SDL3 production build and scoped whitespace check pass. Four
cooperative cases pass: zero/one/two builtin calls in the native owner, plus
one call on the ordinary default selected-native path. They execute the real
loaded QC input transform, public/private command receipt, world physics,
single roomscale application and full server-message parser. A prepared actual
trigger nests another loaded hook/builtin; enclosing solver scratch and the
third cursor-vector component remain intact. Invalid scalar, excessive duration
and zero-time probes exercise the same builtin backend before VM error reporting.

Consolidated existing stock pause/arrival/native-return, q30 normal-session
replay, q30 water publication, older AD shared-QC boots/composition and native
customphysics/Think checks also pass. Exact reproduction is in
[tests/README.md](../tests/README.md#cooperative-quakec-native-movement).

Limits: the hook is assembled over licensed stock QC; starts, nested callback
composition, resources and delivery are prepared/captured. This is real
loader/VM/collision/send/parser evidence, not arbitrary authored-mod, socket,
headset or client-side cooperative prediction proof. The selected-native case
retains authoritative correction and explicitly denies predictive replay.
Stages2–3 and the full migration scope remain required.

The subsequent [accepted-command adapter](predictive-cooperative-commands-2.0-plan.md)
now executes cooperative private heads individually through the same lifecycle
helper, keeping native snapshot classification/correction. It supersedes the
stage1 selected coalescing behavior; public/native-world behavior remains.
Its software/review evidence and outstanding Gorilla/replay/state work are
recorded separately.
