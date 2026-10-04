# Honey desktop slope and movement reconciliation

2026-10-04. Before-code analysis and senior-review brief. Solo operator; no
movement rewrite, new protocol, or per-mod movement workaround.

## Goal and environment

User reports constant desktop rubber-banding and pronounced judder while
ascending slopes on Foundry, after switching to Honey. Fix actual disagreement
before considering presentation. Preserve QC gameplay, native desktop/VR
cross-play, existing q30 low-jump behavior, and upstream vkQuake architecture.

| Fact | Evidence / limits |
| --- | --- |
| [verified: git] Only local `quakespasm-2.0`, branch `2.0`, is writable. | Clean tracked source at v2.0.0/90274914 before this task. |
| [verified: live console] Foundry runs the released ARM 2.0 process in Docker TTY, Honey/start, coop1, skill1, autosave1. | Fresh console query and unchanged process/UDP26000 on switch. Server subsequently logged player selecting `shared QC predictive movement`. Player has disconnected. |
| [verified: configuration] Foundry uses sys_ticrate0.025, gravity800, maxspeed320, accelerate10, friction4, stopspeed100. | Live-installed id1/codex_coop_server.cfg. Some obsolete cvars warn; no proof that these warnings cause movement errors. |
| [verified: local config] Desktop Honey config enables cl_predictmove1 and host_maxfps250. | Installed Honey config. No local engine currently running; actual settings of user's previous run remain unverified. |
| [verified: source] Generic unaware QC executes one PreThink/PostThink world window around a reserved prefix of duration commands. | sv_phys.c:SV_Physics_Client, SV_PrivateWalkTrialExecutablePrefix, SV_Physics_ClientPrivateWalkTrial. Reuse this owner. |
| [verified: source] Server sets qc_jump_owner for exact q30 OR generic shared_qc. | sv_phys.c near10743. Actual QC jump/swim writes must not be duplicated. |
| [verified: source] Client seeds qc_jump_owner from MOVEFLAG_QC_JUMP_ORDINARY only, and this flag is produced only for negotiated exact-q30. | cl_main.c:CL_ComputeReplayPlayerMovement, sv_main.c:SV_PrivateWalkTrialBuildMoveVars. Honey receives ordinary engine-compatible replay. |
| [verified: source] qc_jump_owner additionally changes the rising ground cutoff from180 to0, and enables dry-flat post-snap velocity clipping. | pmove.c:PM_CategorizePosition near1402 and PM_ReconcileQCJumpGroundVelocity near1508. These are more than jump input ownership. |
| [verified: source] PM_CheckJump returns immediately for qc_jump_owner. | pmove.c near1533. Simply setting/removing that field everywhere changes jump/swim behavior. |
| [verified: reference] QSS-M and inherited generic ground categorization use180 threshold. | QSS-M/Quake/pmove.c:PM_CategorizePosition; inherited quakespasm-openvr PM_CategorizePosition. Preserve the existing solver where compatible. |
| [inference] Uphill collision produces positive Z velocity, so server/client can disagree on ground contact, friction and forecast despite identical no-jump commands. | Source-derived; targeted actual Honey regression is being implemented, not yet measured. |
| [unknown] Constant rubber-banding may also reflect snapshot precision, authored QC forces, duration backlog, or ordinary client replay error. | Do not claim slope finding exhausts the report. Need same-command numerical comparison and native connected check. |

## Proposed minimal fix and decisions for Astra

Current lean: separate jump-input ownership from ground/takeoff rules in the
existing solver. Generic shared QC should retain its authored impulse and
swimming, but no-jump slope categorization should agree with ordinary replay.
Exact-q30 requires its existing short low takeoff protection. A second caller
currently borrows qc_jump_owner in the standard QC builtin and must be checked.
The dry-flat inward-velocity reconciliation also differs between generic server
and client; determine whether generalizing that constraint, restricting it to
the negotiated q30 policy, or making it conditional on actual takeoff is safest.

Options:

1. Restrict special zero-threshold and post-snap clip to existing negotiated
   ordinary-QC policy rather than all QC jump owners. Smallest change, no new
   wire state. Risk: generic authored low jumps are not that negotiated policy.
2. Separate an explicit actual QC takeoff observation from jump ownership.
   Retain slope support while preserving low authored launches. Risk: introduces
   additional transient state; replay must receive or infer identical semantics
   rather than silently pretend arbitrary QC writes are replayable.
3. Advertise the existing q30 ordinary-QC replay contract for every mod. Reject
   as a default: generic jump height, release, wet behavior and callbacks are not
   known to obey that exact consumer; no guessed mod-specific whitelist.
4. Turn off prediction or enable smoothing by default. Reject as the permanent
   fix; loses requested predictive behavior or hides authority divergence.
5. Replace solver/queue/scheduler. Reject without demonstrated incompatibility.

Priority is a bounded producer/consumer correction, not a new movement layer.
Slope grounding and inward-velocity clipping may be two faces of the same
overloaded qc_jump_owner field: merging them is explicitly in scope.

## Verification and ownership

Main owns analysis, review synthesis, production integration and live deployment.
Sol high owns only a new targeted native fixture/runner, reusing existing real
QC/BSP, private command, snapshot and client replay helpers. It compares the
same completed command sequence, including flat/uphill/downhill, realistic
25ms world frames and shorter command prefixes. No live-server edits by agents.
These captured transport checks do not establish real UDP pacing on their own.

After implementation: run targeted before/after differential where practical,
existing q30/stock/shared-QC regressions and one private isolated native UDP
Honey movement check. Build Linux plus ARM only if needed to deploy Foundry.
No Windows rebuild, full release campaign, GPU reset, user-profile mutation,
or destructive live restart while players are present. If deployment requires
restart, verify any active-player checkpoint first. Finish with committed source
and explicit scope of the demonstrated fix; subjective user comfort is not an
automated claim.

## Senior request

One Astra xhigh, read-only. Verify load-bearing code first, challenge this
framing, rank concrete risks and choose the smallest defensible correction.
Identify any obvious independent error causing constant flat rubber-banding in
the adjacent completed-command/solver/snapshot path, without an unbounded
network architecture audit. Return <=1300 words with file/symbol evidence,
recommended production write set, meaningful regression requirements and
honest limits. No edits, subdelegation, builds, live server commands or broad
renderer/audio/menu/release review. Main records dispositions before edits.

## Astra disposition before production

Main verified the reviewer's effective `gpt-6-astra` / `xhigh` in the matching
turn-context metadata. Main spot-checked the ground seed, Honey's actual
PlayerJump predicate and +270 impulse, and PM_PlayerMoveQCReplay's deliberate
support restoration and takeoff clearance. User confirmed descent is fine,
ascent judders, and jumps immediately snap back; this supersedes the earlier
possible flat-ground framing. No physical device behavior is being claimed.

| Recommendation | Disposition |
| --- | --- |
| Preserve actual post-QC support for QC jump owners. | Adopted: current ground flag/entity only; retained pre-QC fallback remains stock-only. |
| Apply QC's positive-rising exclusion only when already airborne. | Adopted: supported slope movement reaches the actual ground trace; real QC takeoff stays airborne. Ordinary180/Gorilla exclusions retained. |
| Leave jump/release ownership intact. | Adopted: no guessed impulse, policy advertisement, or second jump. |
| Check landing onto uphill support before declaring completion. | Adopted: actual walkable collision can create positive Z while airborne; add a narrow collision-to-support probe handoff only if demonstrated. Do not enable pground globally. |
| Verify swimming as well as dry slope movement. | Adopted: Honey swim input can author positive Z without clearing FL_ONGROUND; preserve gameplay and compare actual callbacks. |
| Change flat clipping, queue pacing, protocol or default smoothing now. | Rejected absent evidence: leave these owners unchanged. |

Production initial write set remains Quake/pmove.c and Quake/sv_phys.c. The
focused native fixture owns its own two test files; implementation and tests
do not share a write set. Completion requires uphill walking/jump, landing and
re-jump, descent control, q30 short low takeoff/release, and swimming checks.
