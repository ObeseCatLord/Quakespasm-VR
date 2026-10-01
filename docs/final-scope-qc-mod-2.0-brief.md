# Final QC/mod scope review packet

2026-10-01. Mostly verified main-loop packet for local Astra xhigh critique,
not a completion claim. Read final-scope-enumeration-2.0-brief.md for pins,
scope and execution restrictions. Read-only review of exactly MOD-001,002,
004,005,006,008,009,010,012,013. MOD-003/014 and MOD-011 belong to the lead.
No production changes or execution before the final checklist is delivered.

## Main source evidence and lean

All ten rows lean S (integrated, final qualification pending), subject to actual
consumer review. The previous compatibility plan's initial missing list is
historical: its subsequent source checkpoints record implemented adapters.
Do not convert unexecuted VM coverage into a new implementation mandate.

| IDs | Verified current source and bounded receipts | Question for critique |
| --- | --- | --- |
| MOD-001 | pr_ext.c6494 assigned-number lookup; 6780–6893 numeric/named binding; 7002 native lazy table/VM enable; pr_edict.c2296–2297 loader enable/EX patch. qc-builtin-compatibility-2.0-plan.md, qc-builtin-slot, qc-capability-query, qc-core-discovery, qc-debug-fallback plans document occupied-slot and VM policy adapters. | Any concrete missing handler/signature/permission, versus literal-name coverage? The 512-row metadata screen alone is insufficient. Preserve native lazy dispatch and explicit unsupported stubs. |
| MOD-002 | cl_main.c1938–1958 committed-preview policy, 2734 actual outgoing command callback; sbar.c903 actual HUD execution and 1406 selection; migration-csqc-input-review.md and qc-hud-clock plan identify once-frame serial/task owner and VM restoration. | Any missing inherited HUD/view/input caller? QSS-M raw InputEvent is an unselected addition, not evidence to replace current input owner. |
| MOD-004 | pr_ext.c3293 search ownership, 3433 native files, 3608 fseek (cache reset only after successful native seek), 3677 buffers shutdown/current VM, 3698 allocation, 4077 buffer load. qc-file-content, qc-resource-ownership, qc-string-buffer-repairs, qc-file-search and qc-string-token-source-audit receipts. | Identify actual content/lifetime error only. No second resource/filesystem policy. Native path precedence and immediate write errors deliberately retained. |
| MOD-005 | pr_edict.c80–145 free-list owner; pr_cmds.c1400 find and1774 nextent now filter inactive clients; 1279–1393 inherited round helpers. qc-inherited-round-search, qc-entity-search, qc-reflection, qc-inline-surface, qc-number-vector-source-audit and qc-named-calls plans cover current consumers and source repairs. | Any unclosed concrete math/reflection/entity/surface/function contract? Extreme/malformed runtime cases remain final qualification, not proof of absent implementation. |
| MOD-006 | pr_edict.c2047/2072 EX binding and low-slot normalization; inherited FGD text2205b59c and rerelease localization9b71bea2 documented in rerelease-localization-source-2.0-plan.md. Native localization owns message formatting. | Confirm actual text consumers and low-slot semantics; avoid duplicate localization layer. |
| MOD-008 | mod-entity-consumers-2.0-plan.md records actual alpha/effects/static/CSQC native consumers and repairs05e881cc. | Verify repaired model/light flag consumers. No new unsupported drawflags renderer merely because names exist. |
| MOD-009/010 | Native r_part_fte/r_part/cl_tent owners remain. particle-type-growth and particle-emission-precache plans record allocation/name/setup/reader-order/lifetime repairs; qc-weather/explosion-palette plans actual event producers. | Confirm native scripted/effectinfo/weather/trail/beam/temp effects reach their owner. Primary bloodstains are compiled UNSUPPORTED, so not an inherited production obligation. |
| MOD-012 | Native spawn admission/FIFO remains; save-entity-reference-2.0-plan.md records7485407a saved-reference detachment and post-load list reconstruction; pr_edict.c1835 shutdown lifetime. | Any actual restoration/spawn ordering mismatch remaining? Do not replace native edict lifecycle. |
| MOD-013 | Explicit default binds6aff7371, deliberate paused/recovery impulse latchc8d5545a, existing exact-loopback phase query and pre-CSQC projection documented in paused-impulse-adapter-2.0-plan.md. Existing game/mod switch owners remain. | Any missing inherited bindlist/switch/input consumer? User waived legacy aliases, not ordinary commands. |

## Review boundary

Supplemental final-scope-interface-history-2.0.md routes all command/cvar,
history and preservation evidence. All 256 MAIN and 256 XR names have literal
current candidates, not proof of VM execution. Require compact ID/class table,
actual file/line and reference for any M; exact remaining question for Q.
Maximum1200 words plus table. No tests/builds/probes/game runs, telemetry reads,
asset edits, nested agents or scope expansion. If evidence is too large, state
the precise unclosed contracts rather than claim blanket certification.
