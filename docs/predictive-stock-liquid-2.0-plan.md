# Stock liquid movement and predictive replay

Status: reviewed swim handoff correction implemented; remaining real-map/wet
replay qualification in progress. Baseline `81318dc3` on `2.0`; this is another
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

Passing normal matrix: stock `e1m1` native and selected at 25 ms; selected
desktop at 10 and 100 ms (the latter uses negotiated solver substeps); selected
VR at 25 ms with duplicate roomscale delivery; stock `e1m2` native and selected
at 25 ms, including an actual ledge discovered with pinned QC and exercised
through the admitted command path. Every selected desktop generated command
has an actual **diagnostic pending shadow**, not an empty-history substitute.
Flat-water displacement/velocity differences stay within bounds derived from
1/8-unit velocity encoding plus float arithmetic. Full snapshot ACK, owner and
authoritative jump/waterjump timer seeds agree. Actual replay is invoked after
the snapshot and remains forbidden while wet or waterjumping.

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
