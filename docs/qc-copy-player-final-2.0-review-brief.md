# Loaded copy/player queries: bounded senior-review brief

2026-10-01. Solo operator. Main's before-code
[plan](qc-copy-player-final-2.0-plan.md) reuses native allocation/VM/world/
scoreboard owners. Existing production source repairs are documented in the
[source plan](qc-entity-copy-player-query-2.0-plan.md). No replacement service,
entity heap or scoreboard policy is proposed. Reviewer owns no files; main owns
integration/receipts/docs and any necessary corrections.

| Environment fact | Evidence / current status |
| --- | --- |
| Current production | [verified: native source] pr_ext.c4145/4171/5679 retains payload-sized copy, self-copy skip, free-source preflight and allocated scoreboard bounds. |
| Behavioral reference | [verified: primary source] readonly quakespasm-openvr/Quake/pr_cmds.c5851/5871/3002; primary allocation-order/SSQC-only link assumptions are intentionally adapted at existing native boundary. |
| Native layout/allocation | [verified: source] pr_edict.c77/157/313/2290, progs.h55/386. Native FIFO/stride/debug identity and actual full QC payload are independent. |
| Spatial ownership | [verified: source] world.c2886 current-VM area nodes/link/box; host.c993 actual client loader initializes own heap/area nodes. |
| Scoreboard ownership | [verified: source] cl_parse.c2170 allocation formula; sbar.c486 native sort; cl_main.c246 frees scoreboard and VM. Prepared score records are inputs, not full graphical serverinfo parsing. |
| Reset safety | [verified: source] CL_FreeState with null native entities/statics avoids BLAS owner; r_vrik_render.c150 only CPU publication fields, PMCL_ClearMoveVars only validity flag. No renderer acquisition or graphical/device launch authorized. |
| Current executable input/results | [unverified until source completion/run] Private qc-copy-player-current profile will hold actual argv/status/logs, prefix/variant preservation and matching source/binary hashes. |

Workspace /home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0. Private profile
/tmp/qsvr-final-qualification-thchgzi8/qc-copy-player-current. Sources are exact
tests/qc_binding_program.py, tests/qc_binding_native_fixture.c, unchanged actual
host adapter/native owners. Local review must read actual final source/receipts
before critique. No delegation/edits/branch/build/tests/runtime/devices/GUI/raw
session telemetry. GPU stays stopped. <=650words prioritized file/line evidence,
scope_done/assumptions/openrisks/followup; report missing evidence instead of
expanding to other families or another broad goal inventory.

Open decisions: lean retain minimal native adapter and accept precisely bounded
evidence after actual loaded checks. Challenge full-payload/header/neighbor
oracle, free-source FIFO/limit preflight, self/no-touch spatial updates, scoreboard
allocation/sorted bounds and independent-VM teardown. These are one native owner
boundary wearing several wrappers, not separate services. Replacement world/heap/
sorter/protocol rejected because no demonstrated incompatibility requires them.
No malformed raw-reference/nonfinite conversion/free-index debug-policy,
shallow zoned-string ownership, authored HUD/network/rendered consumer,
ARM/Windows/hardware/performance/error-unwind/full F03/F10 claim.
