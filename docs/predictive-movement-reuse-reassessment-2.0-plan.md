# Reopened movement reuse and mod-special-case decision

Status: architecture reopened, reviewed by local Astra Max and resolved for the
next shared-adapter vertical. This plan/disposition precedes production changes;
the vertical below is not implemented yet.
Keep the full migration goal: QSS-M-style modern predictive desktop/VR crossplay,
QC/mod abilities, inherited VR contacts/roomscale and deliberate QBJ3 ladder fix.
Working boots/AD pickup behavior is reference evidence, not permission to write
a new ability implementation. Main/code only on2.0; user-dirty migration doc stays
untouched. Hardware/performance/Windows/ARM checks remain deferred.

## Verified evidence and unknowns

| Claim | Evidence / significance |
| --- | --- |
| [verified: installed pack/program bytes] AD pak2 and q30 program are identical. | SHA2565e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340,2347206 bytes. Actual game ad normal session and boots modes pass existing exact-program assertion and software vertical. No AD-specific implementation is necessary for these bytes. |
| [verified: current production]2.0 admission pins stock or exact q30; native-state checks name q30 fields/ability bits/scheduled attack roots. | sv_main.c:862–915,1648–1684; sv_phys.c:7680–7740,8112 and private walk/native boundaries. It preserves tested QC behavior but makes broader mod prediction depend on repeated qualification and exceptions. This must not silently become the final generic compatibility architecture. |
| [verified: QSS-M source] QSS-M can choose per-command PMove without a program whitelist. | QSS-M/Quake/sv_user.c:635–706 chooses usingpmove from SV_RunClientCommand or sv_nqplayerphysics; PreThink, Think, cooperative hook or PF_sv_pmove, PostThink. Its receipt dispatch is not portable into our existing world queue without duplicating execution. |
| [verified: QSS-M source] QSS-M does not promise authored unaware-mod forces in that PMove branch. | sv_nqplayerphysics default1 at sv_user.c:49 keeps unaware mods native. Its explicit independent branch resets all PreThink velocity to saved velocity when no cooperative hook exists (675–686); generic jumpspeed270 in pr_ext.c:2081. This is evidence to compare, not proof that native boots work under that independent branch. |
| [verified: product source] Inherited/current Quakespasm VR has a shared generic command owner with a legacy wrapper. | quakespasm-openvr/Quake/sv_phys.c:5939–6145 calls unaware PreThink once per world frame, then consumes queued PMove commands; cooperative hooks run per command. Its filter at5665–5742 subtracts expected stock QC drag/jump from a velocity delta, preserving residual authored forces. It intentionally stops for SV_QBJ3NeedsLegacyPhysics. Its sv_nqplayerphysics default0/preserve_qc_velocity default1 are in sv_user.c:56–61. |
| [verified: prior source reviews / current API]2.0 already has private queue/completion/epochs, shared QSS-M solver/stats/replay, fresh native frame, VR input/contact and QC input ABI. | Existing movement/mod plans, sv_user.c/private input owners, sv_phys.c/private walk, pmove.c/h, cl_main.c and PR_GetSetInputs. Preserve these working state/transport owners; absence of a cooperative invocation does not justify a second queue. |
| [verified: focused runtime] Actual spawned/contact/timed boots work under current native ability dispatch. | tests/stock_liquid_native_fixture.c -q30boots actual admission/QC/action/expiry. Native/selected VR and desktop runs match112 zero-axis jump samples at printed1e-6; actual AD run passes. The new work adds a fixture, not production boots logic. |
| [unknown] Which generic wrapper best preserves actual unaware-QC gameplay and modern replay? | Neither 'unverified' nor a hash whitelist proves replacement necessary. Compare actual boots and ordinary commands against existing native reference with existing fixtures; avoid building an ever larger branch-proof/callback scheduler before a usable shared path. |
| [unknown] Native prediction/correction limits acceptable under arbitrary QC forces. | Client lacks arbitrary server QC. Preserve gameplay and use existing correction/authority metadata; do not infer a duplicate ability state machine from final velocity or claim exact arbitrary-force replay. Ordinary prediction and usable supported ability behavior remain required. |

## Mostly-worked design comparison for Astra

Right-size: solo operator, retain working transport/server/renderer and existing
QC lifetime. Prefer one shared movement integration with a narrow VR adapter and
QBJ3 ladder compatibility, rather than per-mod implementations. The existing
exact-q30 path can remain a known reference during convergence; do not delete it
blindly or append more hashes/ability gates as the default expansion strategy.

1. **Adapt the inherited generic wrapper at existing2.0 owners (current lean).**
   Reuse once-world unaware QC versus command-local cooperative ABI, shared PMove,
   current accepted queue/input/contact/completion and published epochs. Do not
   copy whole old sv_phys.c or weaker network port. Verify the old residual-force
   reconciliation rather than assume it is correct; when it fails, isolate that
   compatibility gap at QC/solver boundary. Expected first vertical: normal
   private/public session plus actual boots in a second non-whitelisted program,
   from existing driver, before broad map/ability expansion. Need identify one
   specific generic wrapper implementation and exact replay/correction behavior.
2. **Port QSS-M independent QC/builtin semantics exactly into world dispatch.**
   Reuse PR_GetSetInputs, registry, physent/solver; preserve world queue instead
   of receipt-time execution. But unaware PreThink force reset could erase boots
   or grapple and cannot be accepted merely because QSS-M source has the branch.
   Its default native mode alone does not finish modern predictive gameplay.
3. **Keep exact-program ordinary classification plus new gates/hashes.**
   Existing behavior is qualified and reusable, but this path risks making
   callback/ability audits and per-program lifetime policy the entire migration.
   Retain as bounded reference only if a shared adapter demonstrates inability;
   require a deletion/convergence plan if it remains temporary scaffolding.

Rejected without evidence: new per-mod ability solvers, another native/queued
physics service/protocol, whole client/server rewrite, generic force arithmetic
accepted without native comparison, or blanket PMove that deletes authored QC.
Do not generalize valid defensive packet/state checks into a demand to whitelist
all mod bytecode. Program-specific VR weapon/aim/melee bridges with demonstrated
QC incompatibilities are a separate requirement; this review must not delete
those on the strength of a movement preference.

Open decisions for local Astra: which existing wrapper should become the shared
owner, what can be deleted/folded into it, which specific incompatibility requires
an adapter, smallest end-to-end next proof and precise production write set.
Rank by behavioral fidelity and maintenance. Merge repeated ownership questions;
do not re-review OpenXR/Vulkan/graphics or all q30 scheduling closure. No human
approval/taste decision is expected; the user has supplied the reuse preference.
Main must spot-check findings and synthesize adopted/adapted/rejected disposition
before committing the implementation contract. Stop expansion if a second
scheduler/state machine or repeated interactions exceed this focused boundary.

## Verified Astra review and main disposition

Fresh local Astra verified actual sources before critique with effective
`gpt-6-astra` / `max`; main independently verified turn settings. Main spot-checked
PM_CheckJump's qc_jump_owner suppression, the existing command-duration QC scope,
AD's authored pausetime velocity zero, QSS-M's unaware force reset and the
product's assumed-water-drag subtraction. The latter produces +4 residual at
waterlevel2/25ms/velocity100 even if QC leaves velocity unchanged. This is a
source-derived counterexample, not a reproduced runtime failure. Astra reviewed
source and main's recorded runs; it did not execute tests.

| Recommendation | Disposition |
| --- | --- |
| Separate general admission from proving every program's behavior. | **Adopted.** Remove the general stock/q30 identity condition for the new shared path. Retain actual protocol, lifecycle, numeric, hull/input and stat-overlap guards. A new program must not require another callback inventory or qualification hash. |
| Reuse the product's world-QC/command-solver organization, not its guessed force subtraction. | **Adapted.** Integrate at existing SV_Physics_Client/private walk helpers. QC owns actual server impulse/release/ability writes; use existing qc_jump_owner server suppression without q30 identity. Neither reset velocity nor subtract imaginary stock effects. |
| Define clock and executable input-prefix ownership before code. | **Adopted.** The contract below retains one unaware-QC world lifecycle, command solver duration, scheduled world Think and actual-prefix completion. It prevents unsampled suffix gameplay/tracking from being cleared or acknowledged. |
| Generic client prediction may be corrected for unknown QC effects. | **Adopted.** Use existing ENGINE_COMPAT authority/complete seeds and generic forecast. Do not advertise the exact q30 ordinary policy for arbitrary QC or disable replay merely because a boots bit exists. Corrections are explicit; unknown QC is not reimplemented client-side. |
| Use a genuinely different shipped program for the next vertical. | **Adopted.** Explicitly mount unchanged AD pak0 bytecode with hash assertion in the fixture only; normal AD pak2 is identical to q30 and cannot prove generic admission. |
| Keep cooperative QC separate and accurately report its missing invocation/builtin. | **Adopted.** Existing ABI/solver/registry are reused later; this vertical neither falsely claims them connected nor copies receipt-time dispatch into the queue. |
| Delete temporary distinctions as the shared owner qualifies behavior. | **Adopted.** First retain the exact stock/q30 paths as regression references within existing owners, then fold/remove ability and attack-name routing when the shared path passes their meaningful existing behaviors. No new identities/gates are added as routine expansion. |
| Updated boots positive previews resolve prior P2. | **Verified/adopted.** Final review finds no remaining blocker in that strengthened proof. No production boots code is added by the fixture. |

No user permission or taste decision is needed. The user's reuse preference
selects the direction. This is a necessary convergence of a growing qualification
architecture, not an authorization to replace neighboring working systems.

## Next implementation: exact owners and phase/input contract

Production write set: Quake/sv_phys.c and Quake/sv_main.c, owned together by main.
Reuse sv_user.c receipt/credit/retirement, server state, transport and cl_main.c
initially unchanged. pmove.c/h changes require demonstrated missing force/phase
boundary and a recorded revised write set before editing. Test write set:
tests/stock_liquid_native_fixture.c; main also owns plan/index/README. No main
branch, runtime installation or user-dirty migration document edits.

Implement the shared adapter inside the existing client physics/private command
helpers; do not add a second movement entry point, queue or persistent phase
state. Preserve qualified reference behavior during the first vertical, then
converge it rather than leaving a permanent parallel implementation.

1. **Reserve an executable prefix observationally.** Use actual queue order,
   accepted durations, current credit and lifecycle fences. Only prefix input
   supplies this world's unaware-QC lifecycle. Use the first eligible head's
   view/pose and logical button levels. Stop before another unsampled impulse or
   QC gameplay button transition; that head stays queued for the next lifecycle.
   Do not pull suffix roomscale/contact samples into the initial QC scope. The
   prefix is local traversal state, not another reservation queue/clock. Receipt
   and retirement remain the existing owners.
2. **Run unaware QC once on the world clock.** Prepare prefix input through the
   existing input/weapon/view scope, execute PreThink and the existing scheduled
   Think opportunity at world duration/time, then PostThink once after movement
   or its existing surviving continuation. Maintenance retains one world QC
   lifecycle when no command is executable; it gains neither duration nor ACK.
   Do not rerun QC to recover from a phase transition.
3. **Consume only executed movement.** Each reserved command keeps its accepted
   PMove duration, roomscale/contact sample and completion identity. Actual QC
   velocity/release state seeds the shared solver, with server qc_jump_owner
   suppressing a duplicate generic jump. No residual-force guessing or ability
   bit routing determines ordinary dry WALK. Preserve existing contact/melee and
   weapon-pose owners; no separate multiplayer offsets are introduced.
4. **Keep phase transitions explicit.** Preclassified wet/custom-physics or
   demonstrated hold/ladder states use existing native input/QC/physics boundaries.
   After command movement/contact changes state, complete that head once, finish
   the surviving tail once and leave later unprocessed heads queued. Before
   movement, only existing remaining-phase continuation may finish QC/input
   already consumed; never append fresh native acceleration or repeat PreThink.
   If current helpers cannot express a demonstrated transition without losing
   input/effects, stop and reopen this narrow boundary before writing another
   scheduler. Invalidated/dead owners use existing discontinuity cleanup, not
   fabricated normal completion. Never clear/ACK an untouched suffix.
5. **Publish generic forecast honestly.** Existing engine-compatible snapshot
   authority, complete stats/position/velocity/latches/timers and actual completed
   cursor seed ordinary replay. Generic replay predicts unacknowledged motion;
   server QC effects correct it through subsequent authoritative snapshots.
   Do not infer boots charges or claim exact arbitrary-QC prediction. Retain
   relocation/ownership epochs, stat conflicts and malformed-state protection.

## End-to-end vertical and convergence gate

Use unchanged shipped AD pak0 progs.dat2345354 bytes / SHA256
`f3c4218216ea0d3b00db35eef9e72945ee6687006b3e82c37ea2ae33a0eea922` explicitly
in an isolated root with compatible licensed resources. Assert mounted identity
in the fixture only, with no production checksum/boots-field admission gate.
Reuse real offer/spawn/begin, command receipt, QC spawn/setup/contact, native
reference, public desktop peer, full snapshot/parser and replay.

Require positive ordinary horizontal movement and jump replay; actual pickup,
grounded/airborne boots action and charge/expiry with movement input; dry ability
movement through the shared adapter without a boots-specific native diversion;
corrected client motion during the ability and ordinary replay afterward. Retain
the existing112-sample reference as evidence, not a universal1e-6 requirement.
For the new controlled initialized comparison declare a first target of at most
1 unit position and1 unit/second velocity difference per sampled world frame,
matching discrete lifecycle/charge/expiry outputs. Record all observed errors;
an exceeded target must trigger investigation rather than silent widening.
Arbitrary client QC forecast accuracy is a separate, honestly reported limit.

Include one bounded batching/arrival gap: world QC timers/effects do not multiply
with accepted motion commands; contacts, impulses and roomscale remain single
consumption; prefix ACK/retirement matches execution and the suffix is retained.
Keep existing stock/q30 session and hold/camera/attack evidence relevant to
changed owners. Consolidate Linux checks after coherent implementation, then
local Astra integration review. No whole-program branch proof is required merely
to admit a generic program. Windows/ARM/device/performance stay deferred.

Convergence removes general admission's whitelist and folds the exact-q30 ability
and callback-name routing when the shared owner passes those behaviors. Exact
identities remain appropriate for fixture references and genuinely incompatible
QC adapters. Preserve QBJ3's deliberate ladder fix, VR combat/aim bridges and
existing native continuations. Wet/custom physics, all representative mods,
cooperative ABI, local/load and remaining movement scope remain part of the full
goal; temporary dry/native limits do not count as final compatibility completion.

## First implementation evidence and reopened numerical acceptance

[verified: Linux fixture/build] The uncommitted shared owner admits unchanged
AD pak0 through normal offer/spawn/begin. Actual boots spawn/contact, grounded
and airborne jumps, charge/expiry, public desktop peer and engine-compatible
replay pass112 frames with forward20. No production identity or boots logic is
added for this program. Both native/selected initialized runs complete.

[verified: comparison, not accepted yet] Discrete ownership/charge/expiry match,
but the declared1-unit/1-unit-per-second target is exceeded: maximum axis
position error3.23645, velocity error320 at the one-frame landing difference,
and5 horizontal velocity difference on jump/landing frames. The shared path
lands at frame34, native at35. Do not report this comparison as a pass.

[verified: source] Native SV_ClientThink accelerates before QC using the previous
ground flag; shared PMove accelerates after actual QC takeoff. Native
SV_AddGravity uses analytic72Hz-compatible displacement by default; PMove uses
its existing command substeps/ground snap. These are known differing integration
orders, not evidence that a boots solver or another program gate is needed.
The one-frame landing difference can affect PostThink sound/damage timing;
gameplay effects still need explicit observation.

Reopen only numerical/reference acceptance for local Astra integration review:
retain intentional QSS-M-style movement with explicit native/QC lifecycle and
shared-solver proof, or add a demonstrated narrow shared integration boundary.
Do not silently widen thresholds, modify PMove without a revised committed
write-set contract, or create per-mod force arithmetic. World-QC clocks,
executable-prefix input, suffix retirement and remaining-phase transitions remain
the existing committed contract. Main will finish the bounded batching proof
while Astra reviews the production owners and this decision; no broader scope
or user permission is needed.

## Integration-review disposition and narrowed acceptance

Local Astra Max verified the production diff. Main independently confirmed the
load-bearing call/cleanup order and the actual impulse failure and successful
weapon-effect rerun. The main's112-frame native comparison remains a failed1/1
comparison, not a parity pass. Adopt existing QSS-M solver semantics as the
movement reference; keep native QC ability ownership/charge/expiry/gameplay and
world-callback cadence as the compatibility reference. Native acceleration,
analytic-gravity displacement and landing snap order are recorded differences;
landing damage/sound behavior is still an open compatibility check. This changes
the numerical acceptance reference explicitly, without hiding observed errors,
inventing boots-specific arithmetic or modifying PMove's write set.

| Integration finding | Disposition / implementation contract |
| --- | --- |
| First impulse was cleared before the single PostThink. | **Adopted/fixed.** Keep the actual QC-owned edict value across intermediate commands, including an actual QC clear; mirror it in disposable QC input and clear after the surviving tail. Actual AD weapon changes now pass the batched prefix/suffix case. |
| Later roomscale/wet lookahead could skip the world tail. | **Adopted smaller boundary.** Inside the existing command helper, after movement/contact validation, observe only the next executable-prefix head. If native is needed, run the existing current-head PostThink/validation/publication/completion tail once, then defer the untouched next head after debiting current credit. Remove the duplicate shared later-head outer-loop check. No pending-phase bit or second tail scheduler. |
| The last command could replace the originating QC pose. | **Adopted.** Store first QC input/pose only in the existing frame-local think window. Movement/contacts keep their own commands. Temporarily restore originating input for the sole PostThink without repeating angle/recoil/roomscale work, and retain actual QC impulse/angle writes. |
| Living remaining-phase continuation drained physical contacts before movement. | **Adopted.** Keep terminal/frozen cleanup unchanged. For a living shared continuation, pass only the executing head's sequence to the existing native owner; its normal after-movement contact tail consumes that sample. Do not prematurely drain/reset that living command. |
| Shared after-Think continuation reselected dispatch unlike the native reference. | **Adopted.** Retain pre-Think WALK dispatch, as the existing helper/reference does. Do not change desktop dispatch merely because the generic adapter invokes a remaining phase. |
| A generic helper/pending flag could close loop exits. | **Rejected for this demonstrated case.** Moving the lookahead before the existing tail is smaller and retains all validation/publication/cleanup in one owner. |

Production owners and test write set remain unchanged. Next proof adds actual
originating pose and first-command weapon effect with a later roomscale head
deferred, actual QC clearing without resurrection, and living remaining-phase
continuation. Empty contact cursor checks do not certify physical callbacks.
Cooperative hooks, broader wet/local/load/mod behavior and all remaining full
migration requirements stay open. Astra did source review, not runtime tests.
