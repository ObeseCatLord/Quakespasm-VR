# F03 tokens and real event handoff review brief

2026-10-01. Bounded software-acceptance decision, solo operator, no production
architecture change. Before-code plans: [tokens](qc-tokens-final-2.0-plan.md),
[event handoff](qc-event-handoff-final-2.0-plan.md). Main/read-only references,
user migration document and system settings untouched; GPU work stopped.

| Fact | Evidence / status |
| --- | --- |
| Source | [verified: diff] only existing qc_binding_program.py/native fixture extended; existing helpers/native loader/bootstrap/interpreter reused |
| Token owner | [verified: native pr_ext.c1510–1686/6924 and primary pr_cmds.c4730–4904] shared qctoken scratch, per-VM zones, native wider COM_Parse4096 / temporary output1024 |
| Hook owner | [verified: pr_ext.c7024–7038/QCEXTFUNCS_CS] ordinary loader resolves named CSQC_Parse_Event; fixture only compares resolved hook, never assigns it |
| Parser | [verified: cl_parse.c3346–3425/3871] native parser enters loaded client callback, returns to NULL VM, continues time/nop commands; native EOF marks badread |
| Inputs | [verified: section JSON] six original licensed prefixes/version/CRC/entity width preserved; core/resources/files/calls modes unchanged; generated SSQC/CSQC identical |
| Runs | [verified: private logs/status] assertion-enabled strict compile/link/dedicated run0 for tokens and events; matching PASS markers |
| Device boundary | [verified: dedicated Host_Init branch/bootstrap] no renderer/window/OpenXR/audio/input/socket initialization or GPU command |

Private roots beneath /tmp/qsvr-final-qualification-thchgzi8:
qc-tokens-current and qc-events-current. Read run.log, exit-status.json,
compile/link/run argv, section-preservation.json. Main owns subsequent final
freshness/affected calls and token reruns; do not execute anything as reviewer.

[verified: native loaded observations] Both VMs tokenize fitting quoted input,
console equivalent, comma/multichar separated input, exact text/spans and argv(-1).
Individually primed invalid3/-4 returns raw0 or offset-1. Empty/repeat replace
count/content.1024 a's plus space/b yields count2, copied1023 a's plus NUL,
full-source spans0..1024/1025..1026, last b. Both VMs create actual strzone
copies of argv1, preserve them across retokenization and free through QC;
native zone bits observed, freed references never dereferenced. SSQC zone remains
usable across CSQC clear. Switching VM observes shared token scratch, second
VM replaces it, native clear flushes it. No new per-VM token isolation claim.

Main corrected partial Luna/xhigh draft memoization, index reuse, separator
expected text, priming order and missing CSQC zone case before execution. Worker
was stopped/closed at handoff; no worker execution. C estimate100 reopened to180
for readable assertions; no duplicated production owner or new framework.

[verified: native loaded observations] Events option reuses calls readers, adds
ordinary named hook with QC-owned invocation count. Parser consumes native writer
payload followed by time17.25/43.5 and nop under prepared legacy/modern flags.
All eight exact callback values, resolved hook identity, count0→1, later native
time, full cursor and NULL current VM observed. CSQC clear/load between the two
packets retains server program pointer. This is actual parser/event continuation,
not real transport/negotiation or authored mod behavior. Native EOF badread is
expected; message/cursor/flags/time are saved/restored, not every client global.

Decisions: accept these exact claims or identify necessary minimal fixture/claim
corrections. Current lean keeps native shared scratch and memory/hook/parser
owners unchanged; no production defect was found. Replacing tokenizer/parser,
fabricating runtime hook or recovered error continuation is rejected. Token
scratch versus VM-owned strings are separate owners; review should challenge
accidental conflation and delete overstatement rather than add a framework.

Budget: one local Astra/xhigh read-only review, <=650words with prioritized
file/line evidence and minimal changes. Verify artifacts before critique.
NO delegation, builds/tests/devices/GUI/telemetry/file edits/branch commands.
Do not re-review prior callback/file suite, other nine checklist owners, all
numeric/Unicode/separator quirks, GPU/Windows/ARM or create a new inventory.
No whole F03/F10, client GUI unwind, performance or hardware acceptance.

## Final disposition

All actual reviewer contexts verified gpt-6-astra/xhigh by main using only scalar
model/effort metadata. Reviewer verified real current sources/logs/statuses;
no execution/delegation/edits. Main spot-checked both findings and adopted them.

| Recommendation | Main disposition |
| --- | --- |
| Console could reuse scratch from identical basic input | Adopt/adapt: before console invoke existing one-token seed and snapshot; independently observe count1/exact client, then require console count3/exact fitting text/spans. No new generator machinery. Final affected token strict run0. |
| Cite defining4096 capacity | Adopt: Quake/common.h305 defines COM_PARSE_MAX_TOKEN_SIZE4096, common.c73 uses it for com_token, common.c1980 passes countof(com_token) to native parser. This is a4096-byte buffer; loaded case proves1024 source bytes/1023 copied output, not every capacity-boundary case. |
| Accept real event continuation | Adopt with existing limits: actual loader hook, parser/VM transition, native payload outputs, callback count, later time and full cursor pass; prepared flags are not real negotiation/transport. |
| Keep scratch and zoned-string owners separate | Adopt: native shared tokens/per-VM zones unchanged; no parser/dispatcher/memory-owner framework or production edit. |

Final token console oracle executes from different witnessed scratch in both VMs;
strict compile/link/run0. Event and original calls passes followed shared-reader
refactoring, before the later token-only assertion correction; their native
executed helper is unchanged. All four existing generator modes remain identical.
Previous token passes remain .before-event/.pre-review. No whole F03/F10 claim.
