# Astra: bounded stock swimming handoff implementation

Solo fork, no enterprise ceremony. Review the demonstrated swim-button
correction in the final section of
[the preimplementation plan](predictive-stock-liquid-2.0-plan.md). This is a
force-handoff correction and real-map qualification foundation, **not** wet
prediction enablement or completion of the full liquid/migration goal.

| Fact | Verification / boundary |
| --- | --- |
| `2.0` workspace `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`; plan commits `b752b20b`, `821d0b04`, `c5b98405` preceded production edits | Verified commit/source history. User-dirty `docs/migration-2.0.md` is outside all writes. |
| Pinned stock PreThink at depth 2/3 actually writes z=`100` on press/hold | Verified wrapper around real `PR_ExecuteProgram` in admitted real-map driver. Initial pending shadow displacement mismatch was about 1.94/1.71 units, then 0.08/0.105 on held commands. |
| QSS-M restores stock PreThink velocity wholesale before engine PMove; this fork retains a narrow residual-preserving adapter | Verified `../QSS-M/Quake/sv_user.c:679` and `Quake/sv_phys.c` water reconciliation. Blanket restoration is rejected because this fork preserves callback/teleport forces. |
| Stock QC overwrites z after its drag; shared PMove uses the existing later swim assignment | Verified actual trace plus `pmove.c` PM_CheckJump and bunnyfriction ordering. Native coordinates differ intentionally from QSS-M integration; no native-to-selected loose tolerance is acceptance. |
| Production change is one swim-overwrite branch, all-zero pause preservation, and depth<2 on the grounded dry restore | Verified current `sv_phys.c` diff. No queue, timer, protocol, renderer, public policy or selection-default changes. Weapon-Think velocity precedence stays intact. |
| Focused water/slime/lava/residual/zero-pause/ledge precedence fixture passes ASan/UBSan | Verified `/tmp/qsvr-stock-liquid-unit.log`. Hazard arithmetic alone does not qualify hazardous real-map gameplay/replay. |
| Real pending diagnostic comparisons use actual replay implementation compiled into fixture | Verified `stock_liquid_native_fixture.c`. Shadow bypasses permission/fluid gates and is explicitly not live wet proof. Latest geometry/short-command matrix is being completed by main; do not rerun it or claim unknown results. |
| Existing server dry permission and both client fluid gates remain intact | Verified `sv_main.c`, `cl_main.c`. Actual ledge/callback overlap, hazard crossings, live wet history/preview and broad parent acceptance remain outstanding. |
| Captured transport, prepared signon/resource/input, renderer texture boundary; sockets/ptrace unavailable | Verified fixture headers and prior environment failures. Windows/ARM, live headset/eye and benchmarks deferred by user. |

Objective: verify the narrow correction preserves the existing stock QC/PMove
ownership contract and does not invent velocity for a pause or erase scheduled
Think writes. Challenge simplification/deletion opportunities, including whether
the helper branch conditions identify actual stock swim overwrite sufficiently.
Unknown broader liquid cases must remain gated rather than growing this review
into a second movement architecture.

Read-only exact scope: current production diff in `Quake/sv_phys.c`, focused
`tests/private_water_velocity_fixture.c`, source/plan above. Inspect bounded
`pmove.c` and pinned QC reference/driver only as needed to substantiate a finding.
No edits, subagents or test reruns. Main independently finishes real geometry
and command evidence. Return <=650 words: verified findings with file/line
evidence, blockers for this correction, disposition recommendations, remaining
liquid acceptance clearly separate. Verify effective Astra/max through only
turn_context model/effort fields, never paste raw operational telemetry.
