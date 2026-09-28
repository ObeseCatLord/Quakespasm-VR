# Ordinary q30 command prediction and replay

Status: implementation in progress following exact-QC decision proof `489dfe5e`.
Policy negotiation and replay consumption are implemented at `8a0871c1`;
the native-state checkpoint below follows them. Bounded normal admission now
passes the [activation slice](predictive-q30-activation-2.0-plan.md), Linux checks
and final local Astra review. Real ability/trigger and broader mod compatibility
remain open. This is a coherent slice of the
[AD-family plan](predictive-mod-admission-2.0-plan.md); it retains that plan's
complete native-state/session requirements and later AD/Mjolnir/cooperative-QC
scope. It does not replace the migration's movement or protocol owners.

## Behavior and reuse

An ordinary supporting desktop or VR client should enter q30 through normal
offer/spawn/begin, use sequenced command prediction in qualified dry movement,
and remain functional when QC switches to an incompatible ability or lifecycle
state. The client must reproduce the server's live ordinary jump height,
release/held latch, low takeoff support and committed landing policy. Actual
server QC continues authoring impulses and effects. Shared weapon/muzzle offsets
remain the only calibration. Stock prediction and public/native desktop players
keep their existing movement contracts and can share the world.

Reuse the existing pext key/value offer, per-peer offer/reset owners, complete
private movevars/stat delivery, accepted ACK/owner snapshot, command journal,
preview, PMove roomscale/instant-stop, `qc_jump_owner` geometry, native frame/
phase adapters and single completion cursor. `ED_FindField`/`ED_FindGlobal` are
already VM-owned hash-map lookups; add no binding cache or VM lifetime. Qualified
ordinary replay needs a small policy adapter, not a client QuakeC emulator,
per-ability state machine, QC batch wrapper or second movement implementation.

Verified gaps: the current movevar producer publishes vanilla270 instead of
live `map_jumpheight`; generic replay jumps at a different point with different
held-jump timing; old private clients accept QC_COMMAND authority while ignoring
a new policy bit; server `SV_CheckVelocity` clamps post-QC velocity using
`sv_maxvelocity`, which current replay inputs do not contain. See the
[verified review brief](predictive-q30-replay-astra-brief.md) and existing actual
q30 comparison checkpoint. Those facts justify the bounded additions below.

## Local Astra disposition

Main independently verified the reviewer's effective `gpt-6-astra` / `max`
settings in local turn metadata, including the final review turn. The reviewer
could not inspect those settings within its own tool view; its reported
uncertainty does not substitute for that independent verification. No raw
session telemetry is part of this artifact.

| Recommendation | Disposition |
| --- | --- |
| Give “before PMove” an exact pre-QC order. | **Adopted.** Preserve server-equivalent roomscale, grounded instant-stop and retained support before the client jump branch. A synthetic jump first would make positive rising velocity exclude support and change both policies. Apply once per command, never per solver substep. |
| Guard one explicit policy bit with a pext movement-policy mask. | **Adopted.** One new private key/value in the existing reply, no new query/RTT or capability framework. Missing capability leaves q30 native. Preserve offers across ordinary map signon and clear them with existing offer resets. Authority alone does not identify a matching client consumer. |
| Publish live height through the existing jump stat and commit it with the whole snapshot. | **Adopted.** Typed current-VM lookup, no inferred effective impulse, new height history or stale partial snapshot. Missing/unknown/inconsistent policy suppresses replay. |
| Qualify the unclamped domain or withhold unsupported replay because maxvelocity is missing. | **Adapted toward complete behavior.** Carry one private velocity-limit scalar with this policy and consume it at the demonstrated pre-solver clamp boundary. A legal large height cannot be made correct by replacing the authored height with a guessed impulse. The missing consumer is explicit, and a scalar in the existing stats owner is smaller than a clamped-domain movement owner or extensive native-only restrictions. Verify numeric/slot/receipt compatibility before admission. |
| Native state classification and phase transitions precede admission. | **Adopted.** Share the current-state predicate among receipt, dispatch and snapshot permission. Include ability eligibility/startup/holds/cameras, not merely active forces or WALK type. Use fresh native frames before callbacks and the parent plan's phase-aware completion afterward. |
| Keep the ordinary QC latch and support contract. | **Adopted.** Release clears held; a supported released press consumes the latch, clears support and adds live height to Z. Airborne press does not consume release. Keep `qc_jump_owner` through release commands. Create no generic jump-timer credit. |
| Bump the entire profile, normalize generic jumping or add cache infrastructure. | **Rejected.** Existing negotiation distinguishes the necessary compatibility. Those alternatives change working owners or policies without a demonstrated need. |

An additional local Astra Max review of the actual transport/consumer edits
identified three boundaries before their verification. Main independently
verified its effective model/effort in the final turn metadata and spot-checked
the following source findings. q30 admission stays closed during this work.

| Implementation review recommendation | Disposition |
| --- | --- |
| Keep QC positive-rise exclusion during roomscale, use ordinary categorization only for the disposable instant-stop probe. | **Adopted.** The existing roomscale helper categorizes before/after translation; restoring only support cannot undo a low-airborne ground snap. Add a real-QC release/roomscale comparison close to the floor. |
| Define integer companions before checking floating height/limit stats. | **Adopted.** Reuse the existing timer numeric-parser guard for the two new live inputs, preserving their actual float value. Add explicit float-cast-overflow instrumentation, including large finite values. |
| Do not predict from an unrepresentable authoritative velocity seed. | **Adopted.** The existing signed-short eighth-unit snapshot range is −4096 through 4095.875. Make its conversion defined and withhold private replay through the existing permission boundary outside that range. Preserve authored velocity and the wire layout; do not introduce a clamped physics policy. Exercise writer/parser boundaries rather than accepting a solver-only comparison. |

Main spot-checked roomscale/instant-stop ordering, accepted QC_COMMAND replay,
VM lookup ownership, complete-stat commitment, exact ordinary jump writes and
both selected `SV_CheckVelocity` calls. The selected solver publishes its result
after the pre-solver clamp; it does not apply that clamp per substep or append a
post-solver clamp. Preserve those actual boundaries rather than inventing a
physically different limit rule. Astra also confirmed the corrected cooldown
evidence: bypassing both guards detects repeated firing, while bypassing the
frame guard alone does not establish individual necessity.

## Implementation stages and exact ownership

1. **Complete the shared current-state/phase contract.** `sv_phys.c` owns the
   qualified ordinary and required-native classification, using typed current
   q30 fields/globals and the pinned program. `sv_user.c` receipt and `sv_main.c`
   admission/permission must agree. Include real reachable changes from PreThink,
   scheduled Think, impacts/triggers/physical contacts and PostThink. Existing
   native continuation is not permission to rerun callbacks or accelerate late.
   After movement, finish PostThink/completion once, stop batching and preserve
   later heads for the next native frame. Clear selected credit on native frames.
   Check frozen/camera physical-contact eligibility through the existing cursor
   and drain owner, and ensure hold states do not acquire unintended Gorilla
   movement. Any unhandled reachable transition blocks final admission.
2. **Add bounded policy compatibility in existing negotiation.** `protocol.h`
   defines the private offer key and supported policy bit; `cmd.c` emits it with
   the existing offer; `server.h`/`sv_main.c` store and reset the offer beside the
   current fields and require it for q30 selection. Keep public extension masks
   and stock selection unchanged. Unsupported/absent offers do not gain q30
   replay. Existing demo/header layout remains unchanged.
3. **Supply the complete movement inputs once.** A selected movevar builder in
   the existing server owner reuses `PMSV_BuildMoveVars` and supplies live typed
   height, ordinary-QC policy and actual finite nonnegative velocity limit.
   Reuse that result for solver setup and stats to avoid duplicated policy.
   `quakedef.h` reserves one currently unassigned private stat slot only after
   checking actual custom-stat collision; do not reuse a public stat's meaning.
   `sv_main.c` writes it with the policy's complete snapshot. `cl_parse.c` adds
   the corresponding receipt bit/validation when that policy is present; stock
   and old-profile snapshots keep their existing required stat set. Do not
   require an absent new scalar from an otherwise compatible old stock server.
   A missing/non-finite/negative limit or a custom-stat collision prevents the
   unsupported policy from being used; preserve working native playback.
   Guard numeric integer companions before conversion. The snapshot encoder
   must have defined conversion for out-of-range velocities and withhold replay
   when the authoritative velocity is outside the existing encoded range.
4. **Implement the actual replay consumer at the existing command boundary.**
   `pmove.c`/`pmove.h` keep one shared solver. After existing pre-QC roomscale/
   instant-stop effects, reproduce only the verified ordinary jump branch, then
   the actual component-wise pre-solver clamp. Keep server actual QC separate
   from this client adapter so it cannot create another server impulse. Reuse
   the existing support/landing geometry and suppress generic PM_CheckJump.
   `cl_main.c` applies the policy to committed commands and disposable preview,
   seeded from a coherent owner ACK. Zero/repeated preview must not mutate the
   journal or persistent baseline. No jump per substep, synthetic timer credit
   or extra trace/collision owner. Unknown policy never falls through to generic
   replay. Unqualified native/ability snapshots remain unpredicted.
5. **Integrate normal admission and return.** Enable the pinned first q30
   program only together with the working consumer, capability and complete
   native transitions. Initial q30 startup can use selected native frames before
   its initialized dry state becomes predictive; do not repeatedly select/revoke
   a second session lifetime. Publish existing QC_COMMAND authority for this
   command owner and existing legacy authority for native frames, with existing
   epochs and permission. Leave stock defaults/reference behavior intact.

Production write set is exactly the files above. Tests reuse
`q30_movement_native_fixture.c`, the existing native negotiation/mixed-world
fixtures, owner-snapshot/parser and replay fixtures, and their current make
owners/README. Add a new harness only if those owners demonstrably cannot express
the vertical case, after revising this plan. Main integrates coupled changes;
workers receive disjoint subsets and may not broaden architecture, edit main or
touch the user-dirty migration document. Requested coding routes remain preferred;
unavailable routes must be reported rather than silently replaced.

Expected architectural scope: one capability mask entry, one ordinary policy
bit, one additional scalar, one pre-command replay adapter, and extensions at
the existing classification/phase boundaries. Another handshake, clock, queue,
support journal, field cache or force scheduler exceeds this scope and reopens
the decision before implementation proceeds.

## Transport and consumer checkpoint

The [implementation review](predictive-q30-policy-implementation-review.md)
records the source findings, disposition and qualified software evidence.

The bounded capability, live-input producer, complete-stat validation and actual
replay consumer are implemented. Actual q30 QC still authors the server impulse.
Roomscale retains QC rising geometry, with ordinary categorization confined to
the stop probe. Numeric float companions are defined before validation; private
replay is withheld for a velocity that the existing ACK cannot represent.
Collection estimates respect the actual pre-solver clamp while preserving the
authored height. Existing stock/public movement owners remain in use.

Consolidated Linux build and focused checks pass. The
[existing fixtures](../tests/README.md#exact-q30-movement-comparison) compare real
server QC and policy replay on real hulls for held landing, zero/changed/clamped/
huge finite height, airborne press, low release with repeated roomscale and
initial grounded stop, and 125 ms substeps. Position/velocity differences stay
below .01 and release/ground flags match every command in those cases. Physent
collection is a fixture seam, not a normal client session. A separate actual
serialized stats/owner → full parser → movevars check preserved height/limit and
confirmed at that checkpoint that the injected q30 owner remained unpredicted.
The later activation slice permits qualified replay and updates this injected
writer to exercise the actual consumer; normal admission is separately proved.
The actual registry has nine customstats disjoint from223; staged width/collision cases reject.

Address/UB/explicit float-cast-overflow checks cover missing/stale/negative/
nonfinite limits and huge finite height/limit values. Real solver previews
preserve the journal, pending command and authoritative baseline; a held preview
does not repeat the committed jump impulse. Leak detection was disabled for its
sandbox ptrace limitation; address/UB/conversion instrumentation remained active.
Stock/public mixed checks cover actual receipt/QC/physics/full-parser/replay,
startup pause, arrival gaps, native return and staged encoded velocity edges.
Authored physics remains unchanged when the presentation seed saturates.

This is a component-stage checkpoint, **not ordinary q30 activation**. The
subsequent [native-transition checkpoint](predictive-q30-transition-implementation-review.md)
implements shared typed classification, startup/ability/hold/camera predicates,
bounded dispatch and raw Gorilla hold qualification. Complete callback-phase
transitions, real step/ledge/trigger traversals and the smallest normal
offer/spawn/begin/session vertical below remained required at that checkpoint.
Later bounded closure/traversal and the activation slice above qualify normal
admission and prediction permission. Real ability/trigger traversal and wider
AD/Mjolnir and cooperative-QC compatibility remain in the parent plan; they are not retired by these checks.

## Acceptance and completion

Smallest usable vertical proof: ordinary capability offer → actual spawn/begin
selection → real q30 QC commands → completed cursor/retirement → serialized
complete stats/owner ACK → production parser → matching client replay. Injected
selection or a solver-only jump is not that proof. Prepared assets and captured
transport boundaries remain component seams; do not build a network simulator
to disguise unavailable connected testing.

Require low/changed/zero and clamped heights, airborne press without latch
consumption, held landing/release/re-jump, split/batched commands, repeated/
zero-duration preview, combined roomscale/instant-stop/jump and Gorilla input,
custom-stat collision, old/new offer combinations, reconnect/map changes,
incomplete/stale stats/owner snapshots, and native transition/reentry without
duplicate movement, QC effects or completion. Compare server/replay separately
from the known native integrator differences. Existing stock/public software
cases must still work in the same world. Exercise actual QC/trigger branches
where claiming traversal; staged predicates do not qualify authored map events.

Run consolidated Linux checks after the coherent implementation and final local
Astra review/disposition. Live/device/eye testing, performance measurements and
Windows/ARM qualification remain deferred by the user. This slice cannot be
called complete with a selected all-native bit, an unused policy flag, another
dormant q30 jump helper or missing ordinary replay. Reopen before coding if the
scope grows into a scheduler, another movement owner, per-ability emulator,
new protocol layout or repeated interaction repairs.
