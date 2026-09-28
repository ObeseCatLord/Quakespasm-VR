# Stock liquid movement and predictive replay

Status: reviewed ownership corrections implemented; coherent experimental
live wet replay passes bounded Linux/software checks, with final review below.
Original baseline `81318dc3` on `2.0`; this is another
bounded stage of the [predictive movement plan](predictive-movement-2.0-plan.md),
not ordinary selected activation or a complete migration claim.

## Outcome and behavioral reference

An actually admitted stock-QC peer can enter, swim in and leave water, jump
from dry ground, and perform a supported ledge waterjump without disconnecting,
duplicating QC forces or losing completion. Qualify authoritative movement
against native vkQuake and the reused QSS-M solver, then permit matching
private replay through liquid only where the complete command contract is
proved. Desktop and VR share the generic solver; explicit VR swim/roomscale
rules remain explicit. Public/native peers and desktop graphics stay on their
existing owners.

QSS-M `03a498aa` is the generic PMove reference. Its `SV_ReadClientMove` runs
PreThink/Think/PMove/PostThink and restores stock PreThink velocity before its
engine-owned solver. This branch's narrower QC velocity adapter preserves
authored residual forces, teleport pauses and weapon-Think writes. vkQuake's
native input/physics is the gameplay reference; exact per-substep positions may
differ under the intentional QSS-M integrator, but velocity, jump/swim intent,
state, timers, gameplay effects and completion must have an explained contract.
Use the installed pinned stock QC and real BSP hulls rather than assuming the
rerelease QC is byte-for-byte identical.

## Verified starting point and unknowns

| Evidence | Consequence |
| --- | --- |
| `SV_PrivateWalkTrialStateError` categorizes living water and permits depth 0..3; the existing selected solver handles wet WALK. | Do not add another swimming owner or route wet commands into a fresh native world frame. |
| `SV_PrivateWalkTrialReconcileQCWater` removes stock drag/ledge impulse; dry-jump reconciliation and callbacks already share the selected command boundary. | Reuse these adapters; modify only an actually demonstrated mismatch. |
| Server completion exports jump/waterjump timers; replay seeds them from complete authoritative private stats. | Retain one seed and completed ACK; do not resurrect private journal timer propagation. |
| `SVFTE_WriteEntitiesToClient` grants private replay only for dry WALK, no waterjump, no pusher interaction or teleport hold. | Wet simulation is already authoritative but unpredicted. Permission changes must follow complete proof. |
| `CL_ComputeReplayPlayerMovement` stops engine-compatible private replay if history or preview touches fluid, even if it ends dry. | Changing only the server wet gate cannot enable actual liquid replay. Both client gates need the same contract. |
| Stock water handoff tests are synthetic; q30 trajectory comparison stages selection and concerns different QC jump ownership. | Neither proves actual admitted stock liquid movement/replay. |
| The admitted mixed driver now exercises real command production/receipt, QC/world execution, full message commitment and replay. | Reuse this bootstrap/codec/snapshot infrastructure for the stock map proof. Preserve its captured transport/renderer boundary disclosures. |
| Robust pushers retain native world displacement and withhold replay on interaction. | Preserve that exclusion; moving-liquid geometry/pusher prediction is outside this slice. |

Unknowns: exact stock wet/jump trajectory and force/timer handoff, a reachable
ledge waterjump in the installed map, and permission/history/preview behavior
at both liquid boundaries. These need actual-code software evidence before
permission is expanded. No benchmarks or live headset tests are required now.

## Smallest adapter versus replacement

Preferred: prove the existing command/QC/PMove path on real geometry, fix only
demonstrated handoff incompatibilities, and then adapt the existing private
prediction permission and two fluid gates together. Keep queue, credit,
maintenance, Think window, replay history and snapshot owners unchanged. Do not
add a swimming state machine, native fallback after partial command execution,
new solver or duplicated QC interpreter.

Open decision for Astra: is existing engine-compatible authority plus
PREDICTION_ALLOWED sufficient to state the qualified liquid contract, or does
reusing this private profile require an explicit existing-moveflags capability
bit? Lean toward existing permission: both ends of this release are migrated
together and no separate legacy-release interoperability requirement is stated.
An extra bit must solve a demonstrated incompatible peer contract, rather than
turning unknown compatibility into a parallel policy owner. Public PREDINFO
and unqualified AD/QC profiles must not acquire private liquid permission.

Rejected: blanket QC velocity restoration copied without analysis (could erase
authored callbacks/teleport pause); a new native wet frame after selected QC
(duplicates clocks/callbacks); and enabling wet replay from a synthetic force
fixture alone. Reopen the design if the fix grows beyond the existing command
boundary or needs new persistent ownership state.

## Implementation stages and ownership

1. Main verifies the source brief and obtains local Astra disposition before
   production edits. Keep ambiguity about exact QC/geometry explicitly unknown.
2. Build the smallest admitted stock map proof using the existing native
   negotiation/mixed fixture helpers and production send/receipt/snapshot/replay
   chain. Locate valid dry floor and depth 1/2/3 water with the actual BSP and
   engine traces; do not fake contents or inject selected admission. Compare
   native and selected runs under equivalent generated input and world clocks.
   Prefer independent initialized runs over an elaborate whole-engine
   checkpoint that fails to restore spawned entities, RNG or support state.
3. Cover dry jump/release, shallow/deep swimming, sink/up input, entering/leaving
   liquid, ledge waterjump/expiry, and once-only VR roomscale. Complete histories
   and preview must use the same contract. Inspect velocity, flags, water depth,
   gameplay effects, timer seeds and completed ACK, not just displacement.
4. Correct a demonstrated QC/solver boundary mismatch in `sv_phys.c` or
   `pmove.c` narrowly. Reuse QSS-M code where it is the appropriate generic
   reference. If proof supports it, update `sv_main.c` permission plus both
   `cl_main.c` fluid gates as one coherent change; optional existing-moveflags
   adaptation only after the reviewer establishes its necessity.
5. Run relevant Linux/native/replay/codec checks after coherent implementation,
   review the completed slice locally with Astra, document actual coverage and
   commit. Stock selection remains default-off until the parent's remaining
   pause/pusher/callback/default-activation acceptance is complete. Windows/ARM,
   connected/device/eye trials and performance measurement remain deferred.

Expected write set: the named production boundary only if evidence requires
it; reusable fixture helpers, a stock-liquid driver/comparison and focused
replay/snapshot tests; this plan/index/parent documentation. No edits to main,
renderer, network transport, upstream references or the user's dirty migration
document. Coding delegation must use available user-requested routes and exact
nonoverlapping ownership; unavailable routes are not silently substituted.

## Acceptance and exclusions

- Actual stock private admission remains live across all supported wet/jump
  cases. Public/native peer policy stays unchanged. Each generated command
  completes once; redundancy cannot replay head displacement or impulse.
- Compare equivalent native input and QSS-M PMove references on real hulls.
  Explain intentional integrator differences rather than accepting any loose
  positional tolerance. No duplicate jump/drag/ledge impulse, timer decay,
  drowning/damage or Pre/PostThink effects.
- Full private stat/ACK/owner messages establish usable matching baselines.
  Wet replay, dry-to-wet history/preview and wet-to-dry recovery must execute the
  actual replay function, not a solver spy. Older cached timers cannot override
  authoritative equal-ACK seeds. Clear permission for terminal/native/pusher/
  recovery/teleport holds and unqualified QC remains intact.
- Ledge waterjump needs a real reachable geometry/trajectory proof. If no such
  case is found, say so and continue toward it; do not mark the full liquid
  feature complete or enable a broader contract from guessed coordinates.
- Do not claim actual XR actions, connected reliability, Windows/ARM qualification
  or measured performance from prepared/captured component cases. Skyrooms
  remain outside the full goal.

## Senior disposition and completed evidence

Fresh local Astra (`gpt-6-astra`, effort `max`, verified from model/effort fields)
reviewed draft `b752b20b` and bounded source. It supports retaining the command,
QC, PMove and snapshot owners. Production permission changes remain gated on
real-map evidence; the following dispositions precede those changes.

| Review finding | Disposition / concrete next step |
| --- | --- |
| Existing engine-compatible authority and permission suffice for this release. | Accept. No new moveflag, protocol field, wet state machine or timer journal. Reopen only for demonstrated old-private-release interoperability requirements. |
| Stock QC may overwrite vertical swim-button velocity before PMove's later assignment. | Accept as a coverage gap, not a proved mismatch. Trace actual pinned PreThink and pending replay at depth 2/3 with press, hold and release before altering the narrow adapter. Preserve scheduled weapon-Think precedence and authored residual forces. |
| Active waterjump uses `teleport_time`; completion currently captures solver timers before callbacks. | Accept. Qualify actual ledge startup/termination and callback overlap. Authorize active jump only if final flag/timer/deadline retain the solver-owned contract; retain real teleport/callback holds. Do not overwrite callback deadlines or add a persistent timer. |
| The current fluid latch covers water, slime and lava, including substep crossings. | Accept. Qualify every released type or retain rejection for unproved crossings; final depth/type is insufficient. Do not remove the aggregate gate solely from water evidence. |
| Empty-history or diagnostic shadow success cannot prove live wet enablement. | Accept. Add pending history and preview checks through the actual replay function after coherent server/client policy changes. Diagnostic comparisons before permission are explicitly qualification evidence only. |

The first admitted `e1m1` component run locates real depth 1/2/3 geometry and
executes dry jump, shallow/surface/deep sink/up and prepared VR-roomscale cases
through real command receipt, QC, completion and full snapshots. Wet permission
is still off; this initial run does not qualify ledges, transitions, hazardous
liquids or wet replay. The fixture reuses the existing captured mixed-driver
bootstrap and compiles the actual physics owner to inspect its internal helpers;
no production test API or copied contents classifier is introduced.

### Demonstrated swim-button correction (before production edit)

The admitted pending-command diagnostic executes pinned stock PreThink and the
actual replay solver from the preceding complete snapshot. At depth 2/3, stock
QC overwrites vertical velocity with `100` while the current adapter adds back
drag as though that component were still a scaled old velocity. The server
therefore moves approximately 1.94/1.71 units ahead on the initial press, and
0.08/0.105 on held commands, despite matching final `100` velocity. Non-button
sink/up cases differ only by the existing 1/8-unit velocity encoding. This
confirms Astra's suspected handoff incompatibility; it is not a reason to replace
the solver or restore all QC velocity.

Correction: when the pinned stock wet PlayerJump path runs without active
waterjump, remove its known vertical overwrite (water `100`, slime `80`, lava
`50`), restore the preceding vertical component, and retain any residual force.
Keep horizontal drag reconciliation, deliberate zero-velocity pause and
scheduled weapon-Think precedence. Bound the separate grounded dry-jump restore
to depth <2, where QC actually applies its dry impulse. The same shared PMove
continues to own the swim assignment for server/replay. Qualify actual press,
hold and release plus focused residual/pause/ledge cases. This correction alone
does **not** authorize wet replay; transition, ledge, hazard and callback
qualification remain open.

### Bounded implementation review and software evidence

A second fresh local Astra (`gpt-6-astra`, effort `max`, verified model/effort)
reviewed the [handoff implementation brief](predictive-stock-liquid-handoff-astra-brief.md)
and production diff. Main spot-checked its load-bearing conditions against the
helper, unchanged scheduled-Think boundary and actual replay/PMove ordering.

| Recommendation | Disposition |
| --- | --- |
| Retain the narrow overwrite correction; no blocker demonstrated in the bounded diff. | Adopted. Known swim speeds are removed without restoring imaginary vertical drag or replacing the existing movement owner. |
| Preserve all-zero velocity, including an already stationary owner. | Adopted and tested. Otherwise subtracting a swim overwrite after a QC pause could invent a negative vertical force. This proves the handoff, not an entire pause-through-solver contract. |
| Preserve wet residuals and scheduled Think precedence. | Adopted. Dry restore now excludes depth >=2; changed scheduled-Think velocity still bypasses reconciliation. Do not require exact final z equality, which would discard supported residual force. |
| The predicate is not a general callback execution witness. | Accepted limitation. Recategorization/relocation or eligibility changes without a velocity change remain to qualify before default selection/wet permission. Do not expand ordinary evidence into a blanket QC contract. |
| Keep both fluid gates and server permission restrictive. | Adopted. No wet replay, authority/protocol, timer-owner or selection-default change is included in this correction. |

`stock_liquid_native_fixture.c` now reuses actual negotiation/spawn/begin,
baseline codecs, command production/receipt, QC/world completion and complete
client snapshot commitment. Physics/replay source is compiled directly into
the fixture to inspect existing internal helpers, excluding its normal objects;
there is no production test API or copied contents/ledge solver. Separate
initialized native and selected runs use explicit resting starts, real hull
support, release flags and the existing `kill`/`setpos`/recovery owners. They are
prepared comparison states, not proof of a natural spawn-to-pool traversal.

Historical pre-release matrix: stock `e1m1` native and selected at 25 ms; selected
desktop at 10 and 100 ms (the latter uses negotiated solver substeps); selected
VR at 25 ms with duplicate roomscale delivery; stock `e1m2` native and selected
at 25 ms, including an actual ledge discovered with pinned QC and exercised
through the admitted command path. Every selected desktop generated command
has an actual **diagnostic pending shadow**, not an empty-history substitute.
Flat-water displacement/velocity differences stay within bounds derived from
1/8-unit velocity encoding plus float arithmetic. Full snapshot ACK, owner and
authoritative jump/waterjump timer seeds agree. Actual replay is invoked after
the snapshot and, at that stage, remained forbidden while wet or waterjumping.
The later live implementation stage below supersedes those diagnostic-only
permission claims.

The reachable `e1m2` ledge starts and terminates a selected waterjump; the timer
and flag clear on downward motion and dry replay reopens. Native QC's ledge
impulse is `225`; the reused QSS-M solver intentionally uses `310` and its own
horizontal/termination semantics. These are explicit donor-integrator
differences, not a loose native-to-selected positional acceptance. The `e1m1`
search found no qualifying ledge; it does not silently claim one.

An initial `e1m2` shadow comparison failed when real `army_atk5` QC reduced
health 100→84 and changed velocity after movement. The wrapper verified that
external combat force; it was not a swimming-integrator mismatch and no
production force was erased. Movement comparison runs now use actual `notarget`
commands for both peers, retained after prepared respawns. Environmental QC
stays enabled. This isolates movement and explicitly does not qualify combat
prediction or hazardous-liquid gameplay.

Focused residual/zero-pause/water/slime/lava/ledge arithmetic and sender startup
checks pass ASan/UBSan with leak detection disabled for the environment. The
Linux SDL3 build and admitted mixed startup/gap/native-state matrix pass.
The final combined `e1m2` selected real-map run also passes ASan/UBSan: physics,
replay, server snapshot and client parser/demo sources included in the fixture
are instrumented; the other engine objects retain the normal Linux build. This
is not whole-engine sanitizer coverage. Its instrumented compilation retains an
existing `CL_SetInfo` format-overflow warning as nonfatal with the fixture-only
`-Wno-error=format-overflow`; ordinary Linux builds retain `-Werror` and pass.

Still required by the full liquid feature: hazardous real-map damage/drowning
and crossings; arbitrary entry/exit including one-command fluid transit;
callback/teleport deadline ownership; batching/gaps at wet boundaries; VR pending
history and disposable preview through actual **live** wet permission. The
ledge comparison logs do not close every collision/expiry/callback combination.
Windows/ARM, connected/headset/eye trials and performance measurement remain
deferred by user; skyrooms stay outside the goal.

### Next permission stage: plan refinement before production edits

The [verified permission-stage brief](predictive-stock-liquid-permission-astra-brief.md)
records the remaining stock handoff/quiet-frame/completion ownership forks.
Prefer a captured PreThink witness and the existing semantic relocation and
timer completion boundary, rather than another persistent deadline or wet
state machine. Resolve the final flag/timer/hold contract with fresh local Astra
before production changes. Main extends the existing real-map fixture to every
released liquid type and pending live history/preview; these checks must not
be replaced by empty-history or policy-bypassing shadow evidence.

### Permission-stage Astra disposition and refined implementation contract

Fresh local Astra (`gpt-6-astra`, effort `max`, effective settings verified)
reviewed the permission brief and then revised its quiet-frame advice against
the main agent's actual pinned-QC probes. Main verified the completion writes,
semantic epoch hook, hold gate and both client fluid gates against source.

| Recommendation | Disposition |
| --- | --- |
| Use the positive private waterjump timer as the ownership token. | Adopted. Neither a deadline nor a QuakeC flag alone may manufacture a private timer. No persistent Boolean, second deadline, new protocol bit or swimming state machine. |
| Capture the PreThink movement witness before validation and scheduled Think. | Adopted. Compute the known stock velocity correction immediately; apply it only when scheduled Think leaves velocity and the semantic epoch unchanged. Later water/flag recategorization cannot change what QC actually executed. |
| Detect flag/deadline/relocation takeover before PMove and after real callbacks. | Adopted. Preserve callback velocity/origin and authored deadline; cancel the private waterjump on takeover. Semantic relocation also cancels dry-jump debounce. A flag-only takeover releases its unchanged solver deadline. |
| Extend the existing semantic teleport owner and keep callbacks from resurrecting old timers. | Adopted. Explicit no-hold relocation releases selected movement's deadline; identified QC teleports preserve their authored deadline, including a same-value write. Public/native relocation behavior remains on its existing owner. Completion checks the existing epoch before publishing local solver timers. |
| Rewrite all ordinary quiet water/swim/ledge behavior from local QC source. | Rejected after reviewer correction. Actual active-ledge quiet frames keep velocity 310, deadline and command timer unchanged. The local QC source is not the pinned installed program. Normalize only a demonstrated provisional stock ledge write and observe callback takeovers; do not invent PMove, decay or ACK progress during maintenance. |
| Keep teleport backmove blocking independent of a stale waterjump flag and prevent new ledge acquisition during an external hold. | Adopted. A zero private timer plus a future authored deadline is a hold. Reuse the existing PMove hold input for the acquisition guard. |
| Open wet permission only after live history/preview and the released fluid domain are qualified. | Adopted. Current diagnostic shadow results do not authorize the gate change. Stock selection/default and AD-family admission remain separate parent stages. |

New main probes establish two concrete failures before production changes:
an admitted **new** quiet ledge writes stock velocity 225, FL_WATERJUMP and a
two-second deadline while private timer remains zero; an admitted **active**
ledge overlapping a real pinned `teleport_touch` advances the semantic epoch
and authors teleport velocity, but the selected handoff erases its hold to zero.
The prepared trigger/destination use actual BSP traces and world/QC dispatch;
they are controlled test geometry, not a natural map route or live transport.

Implementation order is now: (1) capture stock PreThink outputs and normalize
only the witnessed provisional ledge writes; (2) preserve scheduled Think and
callback ownership, including quiet/equal-ACK updates; (3) cancel timers at
existing semantic relocation and completion boundaries; (4) block solver ledge
acquisition during an external hold; (5) prove actual pending replay/preview,
transit, hazards and batching before the coherent server/client permission
change. Files remain `sv_phys.c`, `sv_user.c`, its existing teleport callers,
`pmove.c`, fixtures and plan; later permission touches `sv_main.c`/`cl_main.c`.

Real slime/lava depth/swim runs pass admitted command/QC/snapshot checks.
Slime health matches native in the current 96 samples. Lava's two late
deep-button health differences have a pinned PreThink trace: identical damage
event frames/times, with native depth 2 receiving 20 damage and selected depth
3 receiving 30 on the divergent event. The reused QSS-M swim trajectory explains
this depth-dependent gameplay difference; health tolerance was not widened,
damage was not suppressed and no coordinates were forced. This is a PreThink
health/depth/deadline observation, not a count of nested `T_Damage` calls.

### Ownership implementation review

Fresh local Astra (`gpt-6-astra` / `max`, verified effective model/effort)
reviewed the [bounded implementation brief](predictive-stock-liquid-ownership-implementation-astra-brief.md)
against the production diff. Main spot-checked the QC interpreter's float
addition and both provisional normalization branches before integrating advice.

| Finding | Disposition |
| --- | --- |
| Double-clock addition then float conversion does not reproduce QC's float-clock addition. | Adopted. The witness now adds `2.0f` after converting the supplied clock to float, matching actual QC. Add actual pinned quiet and command ledge probes at rounding-boundary clocks; nominal command durations alone are insufficient. |
| Quiet velocity/deadline normalization incorrectly couples independent Think outputs. | Adopted. Correct untouched velocity independently. Release an untouched provisional deadline even when Think clears its flag; preserve a changed deadline or semantic epoch. Command normalization gets the same independent deadline treatment. Composition probes use actual scheduled Think followed by explicit prepared field outputs, not a claim those writes occur in ordinary stock QC. |
| Final command reconciliation does not observe every later entity callback. | Accepted bounded limitation. The existing identified semantic hook cancels timers immediately, including later world teleports and equal-value authored deadlines. Prove that hook with a later-world callback composition/full snapshot; do not add a generic journal or callback framework for an unproven stock writer. Arbitrary unsupported later flag/deadline writers remain unqualified. |
| Existing ownership architecture is sufficient. | Adopted. Keep the command/QC/PMove owners, transient witnesses and existing semantic epoch; no extra persistent policy or duplicated state. |

A final fresh Astra Max correction review verified the float-first addition
and independent quiet/command normalization, finding no remaining blocker in
that narrow diff. It confirmed the prepared callback/link/time coverage claims
are appropriately bounded. Complete independent-field qualification still
needs command-time Think counterparts and a velocity-only takeover case;
quiet composition alone must not be described as full callback coverage.
That gap does not authorize a broader state owner or wet release.

Software evidence after coherent correction: Linux SDL3 `-Werror` build;
focused water/callback and pause/semantic-relocation ASan/UBSan fixtures;
admitted 10/25/100-ms ownership contracts; actual pinned rounding-boundary
quiet/command acquisition; independent quiet Think composition; active/quiet
teleports, wet-destination hold and no reacquisition, no-hold setpos and later
identified equal-deadline snapshot commitment. Final normal selected water,
slime, lava, VR duplicate-roomscale and 100-ms `e1m2` ledge runs retain the
existing wire-derived pending-shadow checks. Mixed startup pause, arrival-gap,
lost traffic, wrap, native flight and death/respawn acceptance also passes.

The final admitted contract passes partial ASan/UBSan at both nominal 25 ms
and rounding-boundary clocks, with leak detection disabled. Combined
physics/replay/snapshot/parser/demo sources are instrumented, other engine
objects normal; the inherited fixture-only format warning exception remains.
This does not imply whole-engine sanitizer coverage or live wet prediction.
An initial nominal late-callback fixture counted non-target trigger touches;
its supplied-time override/count now explicitly matches the target player as
well as the trigger/function, without changing production or loosening the
single-target assertion.

A read-only direct-address/store scan of the actual installed stock program
finds `teleport_time` writes in `CheckWaterJump` and `teleport_touch`, and
`dmgtime` writes in `WaterMove`. This supports keeping the existing semantic
hook; it is not alias/interprocedural exhaustiveness or arbitrary-mod evidence.
Broader wet boundary/replay, drowning, batching and command-time independent
Think combinations remain the next qualification stage. Public/native and
selected defaults, authority/capability profile and both live fluid gates are
unchanged by this correction.

### Live wet replay implementation stage

Before edits, the authoritative baseline is `87c2768b`: selected authority and
server/replay timer seeds work, but server wet permission and both aggregate
client contact gates still prevent live wet replay. Finish the pending
command/velocity-only Think composition cases, then implement and qualify the
existing permission as one coherent experimental slice. A candidate gate must
be exercised before it can be declared qualified; do not stage permission in
the fixture or substitute diagnostic shadows for the actual live entry point.

Minimum implementation: the server permits an alive stock WALK/SLIDEBOX owner
at valid finite depth 0..3, with no pusher/pause/recovery exclusion, when its
positive private waterjump timer agrees with FL_WATERJUMP or its zero timer has
no flag/future external deadline. Validate finite timer/deadline fields. Keep
unsupported QC/native/terminal ownership restrictive. Both client fluid gates
are removed together for the existing private engine-compatible contract;
the shared solver still records fluid contact for its own movement rules.
Public PREDINFO, selection defaults and AD-family admission are unchanged.

Reuse the admitted fixture: compare actual live pending replay with completed
authoritative movement, then invoke a disposable positive-duration preview
from the complete snapshot and prove it does not modify committed history or
authoritative timer seeds. Use actual generated VR commands too. Add real-BSP
surface entry/exit/transit, batched commands/snapshot delay, hazards/drowning,
and negative teleport/pusher/pause/terminal checks. Existing wire-derived
movement bounds remain; no health tolerance or invented dry state.

Owners: main integrates the server/client gate and shared fixture changes;
an available authorized coding delegate may own only the new real-map transit
driver. Coding routes must not silently replace unavailable Luna. Review the
coherent implementation and evidence locally with Astra before commit; reopen
the architecture if callback corrections grow into a second state owner.

### Live implementation software evidence

The coherent candidate now uses the existing stock permission for water,
slime, lava and owned ledge jumps, and removes both aggregate client fluid
gates. The solver, command/QC/completion/stat owners and public propagation
remain shared. Selection stays default-off; q30/other mod admission and full
production activation remain separate stages.

Actual live pending replay replaces diagnostic shadows in the admitted liquid
driver, including generated VR history. Complete snapshots supply all seeds.
Positive-duration desktop previews preserve the committed journal, timers,
ACK and owner netstate. Quiet and command Think composition covers independent
deadline, flag and velocity writes after actual pinned Think; these prepared
outputs do not claim natural stock callback reachability.

The new transit driver checks actual BSP entry/exit, three-world-frame snapshot
delay and multiple received commands completed in a prepared longer world
frame, with unchanged wire-derived origin/velocity bounds. Water/slime/lava,
generated VR and 10/25/100-ms runs pass. Real wet pause and arrival gaps recover
through the existing marker/epoch/completion owners, including positive e1m2
ledge timers. The initial pause assertion correctly tested actual replay rather
than assuming cached metadata was always replaced. Final review then identified
the unpause-before-fence ordering gap; the separately planned parser correction
below invalidates baseline/permission for the pause event's source generation
and retains the existing recovery tuple until its actual fresh pending fence
arrives. Final review required causal context for inverse arrival ordering;
the existing reliable ACK body supplies that context without another owner.

Independent initialized native/selected drowning runs have ten equal actual
damage-event frame/time/health/depth tuples through death and native
continuation. Only air/pain expiration is prepared; health/damage are actual QC.
Prepared pusher-marker/nonfinite-deadline/flag-timer negatives deny permission
through complete snapshots; actual pusher qualification remains later work.

Limitations: captured delivery and prepared client resources/signon/input,
controlled starting positions/velocity/support, and callback/link/time seams
remain explicit. One-command dry→wet→dry has actual shared-solver evidence with
a synthetic hull/contents seam, not a real-stock-BSP case. The strengthened
ledge oracle below replaces diagnostic-only differences with explicit bounds
and exact zero-preview solver exceptions.
This is not arbitrary geometry/QC/mod/combat/connected-XR or performance proof.
The final local Astra review and software acceptance are recorded below and
in the pause plan; these claims do not declare the parent movement/migration
goal finished.

### Ledge replay oracle follow-up

The final review correctly separates selected-server/replay agreement from
native/donor integrator differences. Nominal e1m2 live position error is at
most .002121 units, but four dry grounded slope frames log vertical velocity
differences of 36.15..90. The actual zero-duration preview re-enters
`PM_AirMove`; its grounded branch removes the upward component before a
zero-duration move. This is a presentation-step hypothesis, not an accepted
broad velocity tolerance.

Before extending the fixture, qualify it explicitly: require the actual live
origin/x-y velocity within the existing wire-derived bounds through ledge
acquisition/collision/termination. Compare diagnostic history-only output with
completed server velocity using the same bounds, then require every exceptional
live z velocity to be exactly the zero-preview grounded/dry/no-waterjump clip.
Also compare the actual live PM timer seeds with server completion. Do not
replace live replay with the diagnostic oracle or loosen error bounds. If this
hypothesis fails, investigate the specific discrepancy before calling ledge
replay qualified; no new production movement owner is planned.

The oracle now passes e1m2 desktop at 25/100 ms and generated VR at 25 ms.
Live origins and tangential velocities use the same original bounds. Desktop
history-only diagnostic velocities do too; the inherited diagnostic API
deliberately rejects VR commands, so VR uses actual live output and exact
solver-rule assertions. Zero-duration `PM_AirMove` clips upward dry-ground
velocity; zero-duration `PM_PlayerMoveStep` also clears a falling disposable
waterjump timer before the next authoritative command. The latter is a
derived preview result, not a rewrite of the server seed. All exceptions
require their exact grounded/dry/upward or positive-server-T/downward conditions.
Positive previews separately prove authoritative stats/history remain unchanged.

### Live implementation senior-review disposition

Fresh local Astra Max reviewed the coherent candidate from the
[verified brief](predictive-stock-liquid-live-implementation-astra-brief.md).
Main checked its load-bearing parser/replay claims, reproduced the pause issue
with actual captured delivery, and inspected the actual zero-duration solver
branches before strengthening the ledge oracle.

| Recommendation | Disposition |
| --- | --- |
| Fix unpause before recovery metadata; the helper's immediate snapshot hides it. | Adopted. Commit `e72aa276` planned the correction before production edits. Actual plain wet and active ledge reproduction reopened old replay before correction; both now deny it, including delayed pre-pause metadata. Final review exposed the inverse-order ambiguity; commit `59bf0c6c` reopened the plan before adding source context through the existing per-recipient reliable ACK body. Keep the existing epoch/first-sequence/resume owners, with no new field, opcode or body. A newer pre-pause relocation snapshot must also be rejected until a true newer pending fence. |
| Ledge authoritative execution is not bounded replay correctness. | Adopted. Require actual live position/tangential motion and desktop history-only velocity bounds, with only exact zero-preview ground/falling-timer exceptions. No native-versus-donor excuse for server/replay differences. Generated VR remains live evidence rather than a manufactured diagnostic shadow. |
| Keep single-command actual-BSP dry→wet→dry as an open qualification. | Adopted bounded limitation. Existing synthetic-hull actual-solver proof and separate real crossings/batching stay explicit; full-domain/default activation remains later work. |
| Existing movement/snapshot/timer architecture is sufficient. | Adopted. Keep both aggregate contact gates removed for this default-off experiment; retain the shared fluid latch and all authoritative seeds/owners. |

The final correction review and acceptance are recorded in the
[pause plan](predictive-pause-ordering-2.0-plan.md). Neither disposition changes
default selection, admits another QC program or declares the parent migration
complete.
