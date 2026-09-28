# Stock liquid movement and predictive replay

Status: design reviewed; real-map qualification in progress. Baseline `81318dc3` on `2.0`; this is another
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
