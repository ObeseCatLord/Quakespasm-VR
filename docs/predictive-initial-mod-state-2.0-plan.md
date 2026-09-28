# Initial mod states on the existing private command owner

Status: proposed, before production. Extends cooperative command/VR identity
and shared unaware-QC integration. Does not shrink the complete prediction,
state, local/load and desktop/VR migration goal. Gorilla locomotion and instant
stop are [excluded](migration-scope-decisions.md).

## Behavior and verified current state

A valid nonstock mod player beginning in water, flying, noclip, a custom hull,
or an independent customphysics callback should use the existing private command
owner when its normal offer/defaults permit it. The current classifier chooses
the actual solver/native owner and replay permission independently. Initial
selection is not blanket PMove or client prediction permission. Invalid input,
entity references and callback functions must remain rejected.

Verified at `d065d7fd`:

- `sv_main.c:SV_PrivateWalkTrialAdmissionFailure` restricts all initial owners
  to dry WALK/SOLID_SLIDEBOX before its generic pre-begin classification. It
  separately rejects customphysics, while already-selected generic frames allow
  valid customphysics. This is an initial-state incompatibility in admission,
  not evidence of a missing movement owner.
- `SV_PrivateWalkTrialClassifyOwner(client, require_spawned)` already observes
  generic finite hull/flags/type and returns NATIVE for wet/custom/hook/ladder
  states. `SV_PrivateWalkTrialBeginState` uses that classifier without forged
  spawned state. Reuse it.
- `SV_PrivateWalkTrialFrameStateError` already validates scheduled Think,
  command metadata, water, customphysics functions and live ground references;
  its entry check requires spawned/selected, so admission cannot call it before
  `Host_Begin_f` commits those bits.
- `SV_Physics_Client` and its existing native/cooperative/selected helpers
  handle native and cooperative owners. Cooperative living owners use accepted
  command time; incompatible native/custom owners use their existing world
  clock. Publication retains correction-only for NATIVE and qualified replay
  for WALK. No new protocol or movement mode is required.
- Single-slot/missing-connection, loadgame, robust-elevator, private-profile,
  custom-stat overlap and q30-policy guards are distinct. This slice does not
  remove them, nor claim local/load support is finished.

Unknown: general authored mods' initial callback/native return behavior and
initial terminal/dead states. Keep terminal initial admission native for this
slice; do not classify legitimate incompatibility as a malformed packet.

## Adapter versus replacement

Factor the existing observational frame-state validation into one internal
helper with a pre-begin context. Its public frame wrapper keeps the current
spawned/selected checks exactly. A narrow begin wrapper validates knowntoqc,
the matching live actor/profile and the existing pre-begin classification,
rejecting initial terminal/rejected states. Both share actual scheduled Think,
water/command/custom/ground validation. No temporary writes to spawned,
selection, flags, hull or world/QC state are permitted during validation.

After existing admission policy guards, route nonstock owners through that
wrapper, retaining stock's existing initial contract. This deletes duplicate
nonstock dry/custom/reference policy instead of adding state-specific or mod
name branches. Preserve stock and exact q30 reference handling; do not add a
program hash. General native-to-WALK return uses the current classifier and
publication, without re-beginning or resetting the queue mid-session.

Duplicating frame validation in admission risks divergence precisely at the
new boundaries. Setting spawned/selected temporarily to reuse the frame API
misrepresents state and is rejected. A second native queue/scheduler duplicates
working owners and has no demonstrated necessity.

Expected production scope: about60 changed lines across `sv_main.c`,
`sv_phys.c` and one declaration in `server.h`. Reopen if the work needs another
persistent admission or movement state machine. Scope is a general initial
state adapter, not a wet-only or cooperative-program whitelist.

## Stages and acceptance

1. Commit this plan and local Astra disposition before production changes.
   Reviewer verifies classifier/validation/native owner assumptions and
   challenges whether factoring is necessary and whether initial admission
   can enter a state the existing continuation cannot actually own.
2. Implement shared observational validation and nonstock initial admission.
   Preserve existing stock, disabled/public/profile, stat, elevator and q30
   policy decisions and invalid function/entity/state rejection.
3. Add `tests/initial_state_native_fixture.c`, reusing native negotiation,
   world/QC, sender/receipt, shared BSP liquid finder and full parser. Run an
   actual offer/spawn, then prepared initial state, then actual begin. Never
   inject selection/spawn flags. Repeated begin must not reset live queue data.
4. Loaded prepared cooperative QC must demonstrate wet/FLY/NOCLIP/custom-hull
   selected command movement and correction-only publication beside a public
   desktop peer. Customphysics must execute its real callback with replacement
   precedence and return to cooperative commands without another begin.
5. Loaded older AD QC must begin wet on actual BSP water, move through the
   native owner and return dry to the existing shared solver/replay permission.
   No new mod-specific movement code. Invalid scheduled/custom function and
   ground references must retain native admission rather than selecting a peer
   that the next snapshot immediately drops. Preserve validation immutability.
6. Linux build and relevant coherent regression checks after implementation;
   final local Astra source review; record prepared state/transport seams and
   bounded evidence. No headset/performance/Windows/ARM check is required.

Fixture proof must include actual materialized movement, positive completed/
retired command cursors, normal publication parsing and next owner behavior,
not just an admission bit. Generic initial dry-to-native states are not a claim
of client replay for arbitrary QC. Broader local/load, death/recovery and
compatible cooperative replay remain required parent stages.

Production ownership is main integration on `2.0`. Test/document write set:
new fixture, shared fixture Makefile only for its dependency, `tests/README.md`,
this document and `docs/implementation-plans.md`. The user's modified
`docs/migration-2.0.md` is untouched; licensed assets remain external/read-only.

## Senior review brief

Solo maintainer; reuse one classifier/validator/queue/native lifecycle. Verify
the listed source evidence before critique. Rank the validity/owner/initial
context decisions; suggest the simplest reusable boundary. Do not re-audit
renderer, excluded locomotion or arbitrary mod prediction. Read-only reviewer,
max700words with exact source evidence and disposition-ready recommendations.
Main prepares independent fixture setup while review runs; no production edit
before adopting its disposition.

Environment: this worktree only; sibling engines/assets are read-only; Linux
SDL3 builds and captured native fixtures work. UDP/live hardware and other
platforms are deferred. No new mod identity/name policy is authorized.

## Local Astra Max disposition before production

Effective `gpt-6-astra` / `max` was verified locally. Main spot-checked the
classifier, validation body, private stats export/drop and native publication
guard before adopting the following recommendations.

| Recommendation | Disposition |
| --- | --- |
| Factor the observational validator rather than remove gates alone. | Adopted. One internal pre-begin context and current frame wrapper, plus a narrow begin API. Preserve knowntoqc/live/profile and terminal/rejected checks without forged state. |
| NATIVE eligibility alone does not prove private stats publication. | Adopted acceptance expansion. Parse the first empty-queue snapshot before any QC/world update or return to WALK; verify the native classification, selected-owner metadata and denied replay, then execute the command/body path. Reuse validation if an actual exporter incompatibility is found. |
| Keep native world time distinct from cooperative accepted time and preserve the return frame's native fence. | Adopted. Prove current queue completion/retirement, custom replacement precedence and next accepted owner with ordinary packet flows; do not add another scheduler or enable replay on a native return frame. |

The review identified missing initial publication evidence and the risk of
selecting stale-reference/scheduled-Think owners. The new first snapshot gate
and shared validator address these directly; no new protocol or identity policy.

## Reopened publication preflight before production correction

Final Astra review found that malformed optional entity gravity passes the
owner validator but makes the existing private movevar builder fail. Main
reproduced the admission gap on actually loaded older AD with a prepared NaN
gravity field: admission returned eligible and the refusal assertion failed
(exit134). Do not duplicate gravity/cvar rules in the owner validator.

Adopt the minimal existing-builder preflight in nonstock initial admission:
after successful owner validation, call `SV_PrivateWalkTrialBuildMoveVars` into
local scratch and retain native admission on failure. No stock/frame/queue
changes. This adds about8 lines at the existing admission boundary. Require the
malformed-gravity refusal test with repaired native gameplay after begin, then
rerun the relevant coherent healthy-state/default matrix.

The first empty-queue snapshot intentionally rejects ACK0 while no command has
been produced (`CL_UpdateMoveAck`). Acceptance is complete datagram/body/stats
parsing, usable fresh private stat receipts and stale-ACK accounting without
invented selected/replay metadata. Positive command snapshots later confirm
the selected owner and completion. AD dry-return acceptance also requires
fresh horizontal displacement and completed/retired cursor advancement after
relocation, allowing legal time-credit tails to finish on a no-send world pass.
