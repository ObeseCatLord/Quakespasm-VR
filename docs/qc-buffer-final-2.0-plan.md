# F03 loaded buffer and string acceptance plan

2026-10-01. Existing frozen F03 owner, no new product feature. Production
pr_ext.c already carries the copied primary VM-owner checks, sparse-sort tail
clear and string comparison/replacement repairs. Reuse the loaded program
assembler and offline native fixture from the core binding pass; do not copy
their bootstrap, build a compiler/registry or replace native resource owners.

Before code: extend only tests/qc_binding_program.py and
tests/qc_binding_native_fixture.c with an optional resources mode. Defaults
retain the previous core binding cases. Appended QC entries invoke native
builtins through PR_ExecuteProgram, with both SSQC and CSQC loaded normally.
Main supplies disposable assets, builds, executes and reviews outcomes.

Finite contract: independent buffers in both VMs; owned set/get/size/copy works;
foreign get/size/set/delete and both foreign copy operand permutations are
refused without changing the owner's content. Clearing CSQC preserves SSQC
buffer content, then SSQC clear/reload retires its resources. Invalid handles
(nonfinite, negative and huge) cannot access/mutate/free a live buffer. Sparse
sort followed by append/set/delete exercises the copied tail-clear repair.
Populated buffer cvar-list refill and empty-list refill/delete exercise the
freed-array repair. Exact strings and return values are checked immediately,
not only nonnull handles or absence of a crash.

Also execute the cited second-offset/negative-offset comparison cases and
case-sensitive/insensitive replacement triggers in both VMs. Expected strings
are independent literals. Do not call C handlers directly or assign builtin
results. Passing prepared call arguments through appended globals is allowed;
the actual interpreter and native owner must produce observed results.

Coding worker: explicit gpt-6-luna/xhigh, those two files only, at most250 added
lines each. No production/docs/branch/build/game/commit changes. Main's large-map
qualification work is disjoint. Worker is not alone; retain concurrent edits.
Report scope_done, changed files, verification, assumptions, risks and follow-up;
if the bounds or assembler design need broader refactoring, stop and report.

Reuse current strict native DEBUG/SDL3 objects; QC owners have not changed since
their existing accepted build. Any omitted newer production owner is stated in
the receipt, and exact final Linux/ARM freshness remains F10. One affected
post-implementation compile/run; retain failures and fix fixtures without
weakening expected behavior. A demonstrated product counterexample reopens a
narrow production repair plan and local Astra review, rather than a parallel
resource layer. This subset does not close all files/search/callback/entity or
authored consumers in F03. Hardware and listening remain irrelevant here.

Implementation retained the two existing files and optional modes. Main removed
argv filtering in favor of native COM_CheckParm. Verified local Astra/xhigh
identified two oracle gaps; prime each rejected query within its QC entry,
check raw string zero and exact known cvar replacement. Retired-handle probes
prime through native string builtins to avoid allocating a replacement buffer.
Final diff stays inside bounds; affected native compile/resources/core runs
all pass. [Results](qc-buffer-current-2.0-results.md) and
[senior dispositions](qc-buffer-final-2.0-review-brief.md#final-disposition).
