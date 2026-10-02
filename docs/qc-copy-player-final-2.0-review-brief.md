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
| Current executable input/results | [verified: dedicated CPU run] qc-copy-player-current exit-status.json: assemble/compile/host-compile/link/run/completion-gate all0; run.log final QC_BINDING_COPY_PLAYER_NATIVE_PASSED. Actual argv, source/binary hashes and no-work native-target receipt are in the same private profile. |
| Generated inputs | [verified: byte comparison] section-preservation.json: original six prefixes/version/CRC retained,9declared QC words/31functions/114statements appended, SSQC/CSQC identical,12previous variants byte-identical. |
| Main correction / estimate | [verified: source/second pass] Main added pre-allocation source snapshot/unchanged oracle and omitted-copy alpha/interval/retain checks before the passing second run. Original420unique-C estimate increased to437 after keeping native witness/sorted/null-score/palette/positive-control coverage; no new owner/layer. |

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
Concurrent newly authorized Windows project/portability work owns separate
files and must get its own rebuild/source receipts; this earlier CPU result is
not a claim that final Windows/ARM artifacts match the eventual integrated HEAD.
No malformed raw-reference/nonfinite conversion/free-index debug-policy,
shallow zoned-string ownership, authored HUD/network/rendered consumer,
ARM/Windows/hardware/performance/error-unwind/full F03/F10 claim.


## Main disposition before corrections

Local reviewer model/effort gpt-6-astra/xhigh verified by main using exact
turn_context scalars. The reviewer itself had no access to effective settings.
It found no production-wrapper defect and two finite-oracle gaps. Main
spot-checked the source and adopts these existing-owner corrections:

| Finding | Disposition |
| --- | --- |
| Omitted destination only live/different can select an existing entity. | Adopt known native allocated/freed slot, prepared earliest FIFO head, exact returned encoding, spatial membership and independent native metadata/adjacent guards. No allocation hook. |
| Self-copy alpha/interval checks compare live entity with itself. | Adopt comparison against pre-call snapshot values. |
| Trigger positive control overlaps only explicit copy. | Adapt claim: explicit no-touch proof; self/omitted payload/relink proof. |
| Unused FL_ITEM bounds branch has wrong item-Z behavior. | Delete unused branch; assert fixture's ordinary flags0 input and test native ±1 expansion. Production item handling unchanged. |
| Older no-work receipt predates concurrent pmove portability rename. | Record timing limitation; refresh native target/affected guards after implementation finishes. No claim of current integrated shipping freshness. |

These adjustments extend assertions at existing native boundaries, without a
new owner or test framework. Refreshed execution/review receipts remain pending.


## Corrected final receipts

Main applied all dispositions at existing fixture boundaries. Final loaded copy/
player six-stage pass, normal named-call guard and merged-field reflection guard
all exit0. Source/binary hashes and native no-work receipt refreshed after the
MSVC local-identifier changes and Linux target rebuild.12previous generator
variants remain identical. Final462unique-C estimate includes the known-slot
oracle; no new owner. Earlier receipts retained pre-senior. See [results/limits](qc-copy-player-current-2.0-results.md).
