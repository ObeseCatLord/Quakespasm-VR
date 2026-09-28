# Stock liquid ownership implementation review

Solo fork; bounded implementation of the committed
[permission-stage disposition](predictive-stock-liquid-2.0-plan.md).
Baseline `784cfe27`. Main branch and user-dirty migration document are outside
writes. No new persistent state, timer owner, protocol, solver or transport.

## Facts to verify before critique

| Fact | Evidence / status |
| --- | --- |
| Actual pinned-QC new quiet ledge produced 225/F_WATERJUMP/future deadline with private timer 0; active quiet ledge preserved 310/timer/deadline | [verified] Captured admitted fixture before this production diff; actual BSP/QC/physics/snapshot. Prepared comparison state and transport boundary. |
| Actual active ledge then pinned teleport_touch changed epoch/velocity, but the old handoff erased the authored hold | [verified] Contract fixture probe: timer 0, epoch 9→10, teleport velocity 270, deadline 0. First-acquisition overlap did not expose this same failure. |
| Slime/lava admitted command/domain checks pass with diagnostic pending shadows; lava late damage difference is depth-dependent | [verified] Native/selected PreThink damage frames 0/1/10 at equal times. Divergent event depth 2 amount 20 vs depth 3 amount 30. Not live replay or nested damage-call counting. |
| Server wet/active-jump permission and both client contact gates remain restrictive | [verified] No permission changes in current production diff. This review cannot certify wet release/default/mod admission. |
| Water correction output is now captured immediately after PreThink, before StateError recategorization and scheduled Think | [implemented, acceptance pending] Pure output parameter preserves existing zero/residual arithmetic; applied only unchanged velocity and unchanged semantic epoch. Exact q30 impulse untouched. |
| Only witnessed provisional stock ledge acquisition is normalized in quiet maintenance | [implemented, acceptance pending] Exact T=0, prior non-WJ depth 2, post WJ/225/time+2, unchanged epoch witness. Think observes native fields; its velocity/deadline/flag takeovers retain precedence. Ordinary quiet behavior untouched. |
| Existing callback boundary reconciles F/D/epoch before PMove and after contact/PostThink | [implemented, acceptance pending] Positive private timer owns the waterjump. Flag-only takeover clears an unchanged owned deadline. Deadline takeover preserves authored D; epoch takeover clears jump+waterjump seeds and F. Explicit semantic owner decides D. |
| Identified QC semantic teleport cancels selected timers/F immediately and preserves authored D | [implemented, acceptance pending] Existing world hook passes preserve_deadline=true. Explicit setpos/noclip recovery/co-op relocation false releases selected D; public/native behavior untouched. This explicit no-hold boundary deliberately ends an older hold too. |
| PMove cannot acquire a ledge during an external hold | [implemented, acceptance pending] Existing block_teleport_backmove is derived from T=0 and future D, independent stale F; CheckWaterJump uses the same guard. |
| No sockets/ptrace; local Linux build available; actual headset, benchmarks and Windows/ARM deferred | [verified environment/user scope] Do not redesign transport/testing or expand platform scope. |

Review read-only production diff in `Quake/sv_phys.c`, `sv_user.c`, `server.h`,
`world.c`, `host_cmd.c`, `pmove.c`; plan and fixtures only to assess assumptions.
Main is extending fixture assertions concurrently, not changing production while
you review. No edits, test runs, agents, broad migration inventory or local QC
source assumptions. Do not share raw operational logs. Verify effective Astra
model and max effort using only turn_context model/effort fields.

Return <=800 words, ranked findings with file/line evidence and concrete fixes.
Challenge whether the architecture is necessary and prefer deletion. Focus on
callback precedence, quiet no-command ownership, pre/post acquisition and
termination, semantic epoch publication, same-value authored teleport deadlines,
later world callbacks and no-hold relocation. Separate actual bug from an
unqualified contract; propose the minimum needed software evidence. No human
approval needed for routine corrections within this already authorized scope.

## Final correction review scope

The first fresh Astra Max review found QC float-addition and coupled quiet
velocity/deadline normalization defects. Both are corrected in the current
diff. Review only those corrections and the callback-ownership qualification
claims; no need to repeat broad ownership architecture or migration inventory.
Effective Astra/max must again be verified from turn_context only.

[verified actual-code software evidence] Normal Linux -Werror and the admitted
contract driver pass after corrections. `-roundingboundary -requirecontract`
executes actual pinned QC at double clocks where float-before-add differs from
add-before-float, through quiet new acquisition and a completed ledge command.
Actual scheduled pinned Think then prepared deadline-only/flag-only outputs
pass independent normalization checks at equal ACK. Prepared later-world Think
composition dispatches the actual identified teleport via the world trigger
owner; supplied QC time makes its authored deadline equal to the previous
solver deadline. The immediate semantic hook and full snapshot commit timer 0
at the same ACK. These compositions are explicit fixture seams, not natural
stock callback reachability or live networking claims. Native map geometry,
QC interpreter, command/snapshot codec and semantic hook remain actual.

Main is finishing documentation and software checks while final production
stays frozen. Return <=450 words with file/line evidence: remaining blockers
in the corrected narrow diff, or why prior findings are addressed; minimum
remaining qualification and scope_done/model-effort. No edits/tests/agents.
