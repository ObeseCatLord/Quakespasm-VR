# F03 native CSQC event handoff plan

2026-10-01. Software boundary plan, written before implementation.
The [named-call receipt](qc-calls-current-2.0-results.md) proves actual native
message reads and cursor preservation, not CL_ParseServerMessage event dispatch.
Main inspected cl_parse.c3346–3425/3871: FTE cgamepacket checks negotiated CSQC,
switches to the loaded client VM, executes its registered CSQC_Parse_Event,
switches back to no VM, then continues ordinary server commands. No graphics
initialization is needed to execute a bounded event/time/nop packet.

Keep this parser, its native loader extension hook and QC interpreter. Minimal
proof reuses the existing native bootstrap and generated calls consumers. A
test-only declaration named CSQC_Parse_Event must be loaded/resolved by the
ordinary loader, pointing to the same generated reader code; do not assign the
runtime hook directly or add a custom packet dispatcher/transport/error trap.
Add this declaration only in an explicit event fixture generation option so
the already accepted core/resources/files/calls programs stay unchanged.

Build a private native MSG_Write* packet containing FTE cgamepacket and the
existing byte/char/short/long/coord/angle/string/float payload. Follow with
ordinary svc_time and svc_nop. Actual CL_ParseServerMessage must execute the
loaded hook exactly once, expose the same reader results, observe the later
native time update, complete at the real message end and leave qcvm NULL.
Run both legacy and float-coordinate/short-angle flags; reset/reload CSQC with
the server VM preserved and invoke a later packet. Preserve/save native input
message and client protocol/time state. Default end-of-message badread from
MSG_ReadByte is native behavior, not a corruption oracle by itself.

Missing hook/unnegotiated extension negatives are optional separate native
dedicated rejection processes, not recovered client error proof. No runtime
capability advertisement or protocol negotiation success inferred from prepared
input flags. No real socket/network/loss test, authored mod compatibility,
renderer/HUD, GUI error unwind or whole F03/F10 claim.

Write scope after implementation begins: the two existing QC fixtures, small
optional generation flag and native execution branch only. Reuse their helpers;
target <=45Python/<=65C additions, stop if a separate parser/protocol/VM becomes
necessary. Main-owned before-code plan and loaded-hook/native-parser checks;
strict dedicated CPU execution only after the implementation slice is done.
GPU/OpenXR/audio remain stopped. Reference native vkQuake parser/loader behavior
and inherited message wrappers; no rewrite of adjacent working systems.

Main implemented this slice after token worker handoff. Separate --events/-events
reuse the generated calls readers and the existing native read helper. Ordinary
PR_EnableExtensions resolves the appended named hook; native callback count,
results, later time/nop, parser end and NULL VM observed under both flag families
and CSQC clear/reload. No runtime hook assignment or production edit. Actual
strict compile/link/dedicated run0;50functions199statements appended, all original
prefixes/header invariants and core/resources/files/calls modes unchanged.
Affected original calls and token native runs refreshed after helper change.
Prepared negotiated flags are explicit test inputs, not real network proof.
Verified local Astra/xhigh accepted the stated finite event handoff and retained
native owners. [Actual results/limits](qc-tokens-events-current-2.0-results.md),
[dispositions](qc-tokens-events-final-2.0-review-brief.md#final-disposition).
