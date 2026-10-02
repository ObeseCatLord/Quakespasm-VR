# F03 loaded token boundary plan

2026-10-01. Next finite CPU-only F03 subset. Implementation has not started.
Reuse existing qc_binding_program.py/native fixture helpers, loaded licensed
program append and native dedicated bootstrap. Native pr_ext.c1510–1686 token
wrappers use shared qctoken state, COM_Parse and temporary-string owners;
PR_ShutdownExtensions6924 flushes the shared tokens. Do not replace the parser
or invent per-VM token isolation. Earlier [source disposition](qc-string-token-source-audit-2.0.md)
retains vkQuake's4096-byte parser and1024-byte temporary output capacity.

Minimal design: one optional tokens mode alongside existing mutually exclusive
modes. Append actual QC consumers for tokenize/tokenize_console, argv, argc,
start/end offsets, tokenizebyseparator, strlen and strzone/strunzone. Calls use
normal numeric/name binding and PR_ExecuteProgram; no direct static handlers,
synthetic return values or shadow token allocator. Existing modes byte-identical.

Finite cases, in both loaded VMs: fitting whitespace/quoted tokens with exact
source offsets and last-token negative index; invalid positive/negative index
raw0/-1 after witnessed native nonzero priming; empty/repeated tokenization
replaces count/content; ordinary single/multi-character separators with explicitly
verified native spans;1024 ASCII bytes plus space/b retains count2, argv length1023
and full-source offset1024. Compare output contents independently, not merely
string lengths. A zoned copy of a fitting argv must survive subsequent native
token replacement until its owning VM frees it; never dereference freed strings.

Shared-state acceptance is explicit: switching VM alone does not allocate a new
token owner. A tokenization in the second VM replaces current shared tokens;
VM clear flushes them. Do not require counterpart tokens to survive that native
cleanup. VM-owned zoned strings and token scratch have distinct lifetime rules.
No claims about arbitrary Unicode/numeric casts, every separator quirk, authored
command filtering, GUI error unwind, renderer or all512interfaces.

Before coding, main confirms exact declaration numbers and expected offsets
against native code and the read-only inherited source. One explicit Luna/xhigh
worker may edit only the two existing fixture files, <=180Python/<=100C additions;
no delegation/builds/runtime/production/docs/branch/commit edits. Main owns
private inputs, source integration, strict dedicated CPU execution after the
slice is implemented, section/default-mode comparison and bounded receipt.
Stop/report expansion instead of adding another assembler/VM framework.
GPU/audio/OpenXR remain stopped. No new feature enumeration or whole F03 closure.
