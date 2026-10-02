# F03 loaded files/search resource qualification

2026-10-01. Continue the frozen F03 owner using the existing licensed-program
assembler, native dedicated bootstrap and loaded SSQC/CSQC interpreter. GPU
work remains stopped after the NVIDIA fault. No source omission is assumed and
no production filesystem, VM, allocator or protocol replacement is proposed.

Verified source boundaries: pr_ext.c3156 validates paths and prefers data/ reads;
config fallback is restricted. Native file read/write/close and per-VM shutdown
are authoritative; read handles start at1. Search snapshots have16 shared slots,
per-VM ownership and handles starting at0, mounted-path traversal/deduplication,
empty=-1 and finite/bounded query conversion. buf_loadfile appends lines and
buf_writefile skips sparse holes with finite/clamped ranges. The inherited
reference pr_cmds.c5129/5393/5806/5832 and existing file-search adapter plan retain
these behavioral owners; ignored search flags/package filtering are explicit.

Implementation slice: optional --files assembler/-files native fixture mode in
tests/qc_binding_program.py and tests/qc_binding_native_fixture.c only. Preserve
core/resources defaults. Append named fixture functions/globals and real native
builtin declarations; execute only through PR_ExecuteProgram. Assertions require
actual returned values, bytes and unchanged foreign ownership; rejected queries
must be primed immediately through successful calls, then checked for raw zero.
No direct handlers, fabricated returns or assigned internal resource tables.

Main prepares disposable files/pack with independent expected literals. Finite
proof: both VM owners, data-over-fallback read/CRLF/blank/unterminated tail/EOF,
buffer append/empty load/sparse write and exact disk bytes, rejected paths,
foreign file/buffer/search operations, closed/nonfinite query refusal, native
search pack/loose deduplication and packed nested match/loose nonrecursion. Fill
all16 real search slots, reject the17th, retire one and reuse it. Clear CSQC while
SSQC resources survive; reload SSQC and query its old handles before allocating
replacements, then prove retired search capacity is available. Do not promise
stale-handle generation safety after same-VM slot reuse, unsupported package
filters, Windows execution, model-only mounts or all file failure cases here.

Coding contract: one explicit Luna/xhigh worker, those two files only, <=350
added Python/<=240 added C lines. Not alone in checkout: retain concurrent edits;
no production/docs/build/runtime/branch/commit changes. Main independently owns
assets, build preparation and reference spot-checks while worker codes. Return
scope_done/files/source checks/assumptions/open risks/follow-up <=500words; stop
and report missing evidence or required expansion. Native compile/run after
implementation, strict existing SDL3 DEBUG flags and dedicated/no-UDP/no-sound
bootstrap; no SDL window/Vulkan/OpenXR/microphone. A demonstrated source defect
reopens a narrow repair plan and verified local Astra review. This subset does
not close the remaining callback/entity/error/authored-consumer F03 boundaries.

Main integration scope reopening: the native helper estimate becomes300 added
C lines, retaining350 Python. The first diff needed both directions of foreign
write checks while both native writers are still open, immediate invalid-return
observations, and the planned SSQC retirement phase. Reuse the same native
entries/loader, not another resource manager or queue. The small extra phase
does not change production ownership; remove redundant assertions/unused helper
boilerplate rather than weakening actual byte/cursor/retirement oracles.

Final Astra integration keeps the same adapter and raises only the main C
integration estimate to330 lines: each zero-result rejection follows an observed
nonzero native return; each reopened handle has a positive witness. Append an
owner sentinel after reciprocal foreign closes, and read one line before each
VM clear, leaving unread data. Direct-handler probes would be shorter but lose
the required loaded-interpreter/owner behavior; a new resource framework adds
no value. No new product feature, state machine or production rewrite follows.
