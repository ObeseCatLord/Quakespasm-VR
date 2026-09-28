# q30 ordinary replay: missing consumer and compatibility decision

Preimplementation brief for the next coherent admission/replay slice, following
the [reviewed AD plan](predictive-mod-admission-2.0-plan.md). Solo operator;
one movement owner, reuse QSS-M PMove/vkQuake QC and collision. Read-only local
Astra review, <=900 words, no edits/tests/subagents. Verify load-bearing claims
before critiquing. Rank the decisions; give the highest-leverage one a concrete
contract. Do not reopen renderer/device/platform decisions or redesign the
command queue. No human taste decision is currently identified.

## Verified facts and limits

| Claim | Evidence / consequence |
| --- | --- |
| Current tree | `quakespasm-2.0`, `2.0`, plan/disposition commit `355fa8cd`. User-dirty `docs/migration-2.0.md` excluded. Current uncommitted edits extend only the exact-q30 fixture; no production admission/replay change. |
| Actual dry comparison | [verified execution] Current `tests/q30_movement_native_fixture.c` builds under Linux SDL3/-Werror and exits0 with all markers, log `/tmp/qsvr-q30-cadence.log`. Exact installed SHA-pinned q30 executes against stock e1m1 hulls. Forty-eight 8ms commands hold through real landing, release and re-jump: two takeoffs, matching velocity/flags each command; peak position difference0.659554, landing reconverges. The fixture uses a one-unit local displacement bound, not identical native integration. Startup is warmed through native QC, not qualified. |
| Batch/quiet effects | [verified execution] Two/eight selected 5ms commands versus existing native coalesced single-world frames spend one shotgun shell and have the same weapon/currentammo/cooldown. Quiet maintenance before deadline spends none; after advancing to the authored deadline it fires another shell without a new ACK/duration. Impulse1 selects axe once. This is injected queue/input and clock, not real networking or full map simulation; it disproves treating all zero-time QC as inert. |
| Redundant cooldown guards | [verified binary/execution] `W_FireShotgun` PCs69399–69402 independently returns for `attack_finished > time`, in addition to `W_WeaponFrame`'s guard. Test-only frame-guard bypass still exits0; bypassing both returns fails the first selected two-command batch's one-shell check (exit134). This identifies the observed guards, not arbitrary weapon cadence. |
| Native hold adapter | [verified execution] A staged `pausetime` predicate executes actual q30 PreThink with forward input: fresh native frame consumes sequences1–2 without movement/credit, then sequences3–4 move after expiry. Direct native adapter call is a component seam, not production classification or a real teleporter traversal. |
| Server q30 ownership | [verified source] `sv_phys.c:8182–8345` keeps post-QC authored velocity/release and sets `pmove.qc_jump_owner=true`. This suppresses PM_CheckJump; `pmove.c:1399,1505` also excludes every positive rising takeoff from floor support and clips inward velocity only at a committed dry flat-world landing. Stock correction is separate. |
| Ordinary QC branch | [verified binary/source correspondence for load-bearing writes] Installed q30 `PlayerJump` PCs55044–55073 require ground/release, clear those flags, clear button2 and add live `map_jumpheight` to velocityZ. The exact installed identity and its ordinary branch are in the previous review; abilities/ladder/water differ and need native classification. Height may change per map; do not derive it from final velocity or replace ability forces. |
| Existing client consumer is insufficient | [verified source] `cl_main.c:1578–1785` seeds ACK origin/velocity/support/release/timers, then generic PMove and preview. It leaves `qc_jump_owner=false`. `PM_CheckJump` adds generic jumpspeed after categorization/friction and permits its QW timer behavior. It does not reproduce the QC-before-PMove latch/low-support policy merely by setting height120. |
| Existing complete stats transport | [verified source] `SVFTE_WritePrivateMoveStats` sends STAT_MOVEFLAGS, STAT_MOVEVARS_JUMPVELOCITY and other complete movevars immediately before ACK + baseline-relative owner; `PMCL_SetMoveVars` consumes them. Authority/mode/discontinuity epochs already distinguish native/selected state. No new height/support/timer payload is presently demonstrated necessary. |
| Compatibility gate | [verified source] Private profile1 is pinned (`protocol.h:459`). Older clients accept QC_COMMAND authority (`cl_parse.c:2980`, `cl_main.c:1633`) but replay generic movement. They ignore an unknown new movevar flag. Therefore authority or a stat bit alone is not a proved compatible way to grant them q30 replay. Stock/public behavior must remain working. |
| Existing negotiation owner | [verified source] `cmd.c:961–977` replies to `cmd pext` with key/value pairs including separate QSVR profile key. `sv_main.c:3181–3199` reads pairs before spawn and ignores unknown keys. Existing per-client offer reset/map-sign-on owners are reusable. Current Gorilla capabilities are specialized reliable string commands; there is no generic movement capability handshake to copy wholesale. |

Unknown: ordinary server/client movement equality with an implemented replay
consumer; all native-state predicates and reachable mid-callback transitions;
real mod-map ladder/grapple/liquid traversal and complete snapshot/replay sessions.
Admission stays closed until these contracts are implemented together.

## Mostly-worked proposal and alternatives

Main lean: **one explicit ordinary-QC jump policy bit in the existing complete
private movevars, guarded by a capability in the existing pext reply**. A new
private key/value declares a supported movement-policy mask; server stores it
beside existing per-peer offers and resets it with them. It requires no new
query, command-queue lifetime or RTT. Old peers lacking the mask stay native for
q30; public peers/stock prediction remain as today. Do not broaden this into a
general feature negotiation framework.

The live map height uses the existing jump stat. Client applies the qualified
ordinary press/release branch once before each shared PMove command (including
preview), using ACK/final-command support and release state, then uses the same
`qc_jump_owner` geometry and final committed-support reconciliation as server.
Reuse an existing jump primitive if it can express that order and latch without
changing stock/public movement; otherwise use one small explicit shared-policy
adapter for the verified ordinary branch. Server continues executing actual QC;
client does not emulate boots, ladder, grapple, wet/hold or camera QC. Publish
existing QC_COMMAND authority for this owner, permission only in qualified dry
state, and preserve existing authority/mode/discontinuity metadata. New policy
data is useful only with its actual client consumer and session admission.

Alternative1: bump the entire pinned private profile. Simpler compatibility
conceptually, but changes negotiation/demo/header admission for unchanged wire
layout and prevents otherwise compatible old stock/private playback. Require
evidence this is smaller than one bounded capability, not merely a desire to
avoid checking the existing offer parser.

Alternative2: use QC_COMMAND authority alone to select the jump policy. Rejected
provisionally because old clients already accept it but have no matching jump
consumer, and later cooperative command programs need not have this policy.

Alternative3: normalize q30 to generic PM_CheckJump. Rejected provisionally:
late jump categorization/friction, timed held-jump behavior and low support do
not match the verified QC-before-PMove branch. This risks erasing authored forces
or changing adjacent stock policy to make a comparison pass.

Review the required missing input versus duplicated jump policy, compatibility
choice, command/preview ordering, support/release seed, changed-height snapshots
and required session gate. A per-ability client state machine, second solver or
new wire height/ground lifetime is out of scope without new evidence. The
current fixture is decision evidence, not a claim that ordinary q30 admission
is ready or the parent AD/Mjolnir implementation is finished.
