# Generic mod swimming through the existing command owner

2026-09-30. Design brief before implementation. The full migration remains
active. Write only the vkQuake-based `2.0` checkout; do not touch the user's
`migration-2.0.md`. No executable checks until implementation is finished.

## Verified baseline and references

| Fact | Source and implication |
| --- | --- |
| [verified: current classifier] Generic living WALK uses the shared unaware-QC adapter only while dry, with no waterjump, customphysics, cooperative hook, hold or ladder. | `Quake/sv_phys.c:SV_PrivateWalkTrialClassifyOwner`. Stock and exact-q30 are separate retained reference branches. Wet generic motion uses the native world frame and legacy authority. |
| [verified: current dispatch] Nonstock selected WALK additionally runs a callback-free liquid/roomscale lookahead, both before the first head and before a later shared head. Post-move classification repeats it. | `SV_PrivateWalkTrialQ30NeedsNative`, `SV_PrivateWalkTrialPostMoveNative`, `SV_Physics_Client`, `SV_Physics_ClientPrivateWalkTrial`. A classifier-only edit would leave swimming diverted and could break prefix/tail ownership. |
| [verified: existing shared integration] Generic QC owns one world PreThink/PostThink and actual velocity writes, while each reserved head consumes its accepted duration in shared PMove. | Existing shared-QC think window, executable prefix, command solver, completion and remaining-phase continuation. Preserve originating pose, scheduled Think opportunity, suffix input and one-time roomscale/contact consumption. |
| [verified: solver] Server generic shared QC sets `pmove.qc_jump_owner`, so PM_CheckJump does not add another jump/swim assignment. PM_Friction and PM_WaterMove still run. PM_CheckWaterJump can create a solver ledge jump. | `Quake/pmove.c:PM_CheckJump`, `PM_Friction`, `PM_WaterMove`, `PM_PlayerMoveStep`; `sv_phys.c` constructs disposable PMove. Do not silently erase authored swimming or ledge forces. |
| [verified: timer and publication] Stock private liquid timers already have provisional-QC and callback reconciliation. These helpers intentionally do not claim generic QC ownership. Publication permits valid engine-compatible WALK including finite depths0..3, but denies unowned waterjump flags/deadlines. | `SV_PrivateWalkTrialProvisionalWaterjump`, `SV_PrivateWalkTrialWaterjumpCallbacks`, `SVFTE_WriteEntitiesToClient`. Reuse existing timer/epoch/publication boundaries; do not simply reinterpret arbitrary QC deadlines as a private ledge timer. |
| [verified: behavioral reference] Main's inherited generic adapter subtracts guessed stock water drag/swim impulses from QC velocity. | Read-only `../quakespasm-openvr/Quake/sv_phys.c:SV_FilterLegacyPMoveQCVelocityDelta` and `SV_RunClientPMoveCommand`. The earlier accepted reuse reassessment shows that guessing drag when QC made no such write invents force. Do not generalize the stock-only reconciliation. |
| [verified: accepted semantics] Existing shared-QC reuse deliberately uses the modern shared solver as the motion reference, with authoritative correction for unknown server QC forces. | `predictive-movement-reuse-reassessment-2.0-plan.md`. Native trajectory equivalence is not claimed; actual gameplay effects, callbacks and authored state must survive. |
| [unknown] Does admitting ordinary generic swimming with actual QC velocity intact preserve enough of the intended shared-solver/QC contract? | There is no universal way to infer whether an arbitrary QC velocity edit duplicates an engine force. Do not invent a mod whitelist, ability solver or callback journal to pretend otherwise. |

## Proposed minimal adapter and alternatives

Main's lean is a bounded ordinary-swimming extension in the existing shared
owner: remove generic depth alone as a reason for native execution, and restrict
the existing callback-free liquid lookahead to the exact-q30 reference that
actually needs it. Preserve actual QC forces without stock drag subtraction.
Keep native handoff for authored waterjump, customphysics, hold, ladder,
terminal state or loss of a supported hull. An engine-created ledge jump may
also hand off through the existing completed-head tail, retaining the actual
flag/deadline under native authority; do not claim generic ledge replay until
its ownership contract is completed. Ordinary wet input should then retain
accepted command timing and receive existing engine-compatible forecast, with
authoritative corrections for unknown QC effects. No new prediction policy,
wire field, queue, scheduler or persistent state.

This is an incremental swimming slice, not completion of every wet/ledge or
cooperative replay behavior. Retain stock and q30 paths as behavioral references.
Before changing code, challenge whether preserving actual QC forces alongside
the shared water-friction solver is a legitimate modern-motion boundary or a
demonstrated gameplay regression. The already accepted dry adapter preserves
actual QC instead of guessing it; extending that rule does not prove wet parity.

Compared alternatives:

- Copy the inherited residual-force filter: rejected as a generic solution;
  actual source counterexample shows invented force when presumed drag is absent.
- Reset all unaware-QC velocity as the QSS-M independent path does: rejected;
  it can discard authored boots/grapple/swim behavior.
- Extend native wet physics and label it plain replay: rejected; server and
  client would integrate different solvers while claiming one contract.
- Keep all generic wet movement native forever: preserves execution, but leaves
  requested modern predictive coverage unfinished without a genuine incompatibility.
- Build a general QC operation witness or another physics service: rejected
  absent a demonstrated boundary that the existing adapter cannot express.

Estimated production write set: `Quake/sv_phys.c`, under80 changed lines for
classification and dispatch-only restriction. `sv_main.c`, `pmove.c/h`,
`cl_main.c` or another owner requires a revised contract before edits. Main
owns design/integration; Luna xhigh may implement the exact accepted slice.
The coupled file has one coding owner. No fixtures or test changes now.

## Senior advisory request and final qualification

One local requested Astra xhigh, verify before critique. Audit the load-bearing
claims and rank the actual implementation gap against intentional correction
for arbitrary QC. Select the smallest safe reusable slice, or identify the
specific incompatibility that prevents this lean. Challenge architecture,
deletion opportunities and misleading completeness claims. The separate
cooperative replay question may be the same unknown-QC issue; merge it only
if source evidence supports that disposition, without inventing CSQC support.
No edits, nested agents, builds, tests, compiler probes, fixtures, game runs
or broad renderer/voice/avatar re-review. Return at most1200 words with precise
producer/consumer evidence and a concrete coding contract. Effective settings
are not exposed by this tool; report requested-Astra advice, not certified
skill or final-goal signoff. Main spot-checks and records disposition before code.

End-of-implementation Linux/ARM qualification must exercise actual loaded
nonstock QC swimming, press/hold/release, liquid damage/timers/sounds, dry/wet
crossings, queued prefixes/suffixes, roomscale, callbacks, authored and solver
ledge transitions, complete snapshot permission and pending replay, pause/load/
relocation/native return and desktop/VR coexistence. Reuse existing fixtures;
prepared inputs and captured delivery remain explicit. No live headset or
performance testing is required of the implementation goal. Windows is deferred.

## Requested-Astra disposition before production

The requested Astra xhigh advisory supports the ordinary-swimming extension
and found two concrete handoff omissions in the proposed design. Main checked
actual solver result materialization, the native `SV_WaterJump` consumer, the
callback boundary assignment and completed-head timer clearing. These are
source-derived paths, not executed failures; effective model/effort remains
unexposed, so no certified senior-skill or final-goal signoff is claimed.

| Recommendation | Main disposition |
| --- | --- |
| Extend ordinary generic swimming without presumed stock force subtraction. | Adopt. QC-authored velocity is followed by the shared movement solver; ordinary client forecast may need authoritative corrections even for conventional QC swimming. Do not claim numerically identical replay or native trajectory parity. |
| Seed native waterjump horizontal direction before handing off. | Adopt. When the generic shared solver produces a positive waterjump timer, set existing `movedir[0..1]` from completed solver horizontal velocity during result materialization, before any impact/trigger/contact/PostThink callback. Preserve movedir Z and all subsequent QC writes. |
| Latch the generic solver-created ledge boundary before callbacks. | Adopt. Reuse the existing frame-local `native_boundary_completed`, then OR subsequent boundary observations into it. Even when QC cancels that ledge, the current head's existing tail retires private timers, withholds replay for this frame and retains the unstarted suffix. No persistent transition state. |
| Restrict liquid lookahead to the exact-q30 reference and delete its shared-next-head use. | Adopt. Early return from the existing lookahead outside that reference; remove the now-inapplicable shared block. Retain `defer_next_head` and its completion/fence owner for cooperative handoff. |
| Treat arbitrary cooperative hooks as universally replay-compatible. | Reject. Correction-only replacement-hook execution is intentional until a genuinely supported replay contract exists. Calling the standard builtin alone is not such a contract; transformed/repeated/omitted calls cannot be inferred away. This open design does not block ordinary swimming. |
| Keep the original dispatch-only estimate. | Adapt. The same one-file, under80-line estimate remains, but includes result materialization and sticky-boundary changes. No new queue, protocol, timer owner, client policy, solver or per-mod admission. |

Final production contract: Luna xhigh owns only `Quake/sv_phys.c` classification,
the exact-q30 liquid lookahead restriction, removal of shared-next-head liquid
lookahead, generic solver-ledgeresult movedir seed and sticky completed-head
boundary. Stock/q30 reference semantics, QC cadence, existing continuations,
native timer/deadline lifetime, callback order and completion/credit/suffix
owners stay intact. Main reviews the actual diff and requests bounded Astra
source advice before accepting the patch. No tests, builds or fixtures now.

## Source integration checkpoint

Implemented by Luna xhigh in `486a5a49`:13 additions and10 deletions in the
existing `sv_phys.c` owner. Ordinary generic swimming no longer diverts solely
for liquid depth or the q30-only lookahead. Actual QC velocity, jump/swim writes
and one world lifecycle remain; existing command duration, snapshot permission
and client forecast owners are reused. Stock/q30 reference classification is
unchanged. Generic solver ledges seed existing native movedir XY before
callbacks and latch the completed-head native boundary. Later callback edits
survive while private timers retire and untouched suffixes remain queued.

The bounded requested-Astra final source advisory inspected the full actual
diff and producer/consumer chain, reporting no scoped P1/P2. Main independently
checked native movedir consumption, sticky boundary/tail order, private timer
clearing, completed-cursor retirement and native-frame publication exclusion.
Accept the patch under the recorded source limits; no additional implementation
owner or policy was required. Scoped whitespace checks pass.

No executable check, build, compiler probe, fixture, game run or benchmark ran.
Generic swimming remains QC-authored velocity followed by shared solver motion,
with authoritative correction, not exact numerical replay or proven mod/native
trajectory parity. Actual gameplay, Linux/ARM owner round trips and all final
qualification cases remain pending. This closes the bounded ordinary-swimming
implementation gap, not the full movement/migration inventory. Arbitrary
cooperative hooks remain intentionally authoritative without an established
client replay contract; hook/builtin presence alone does not prove one.
