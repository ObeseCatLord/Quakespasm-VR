# F03 loaded named calls and message-reader qualification

2026-10-01. Frozen F03 callback/read boundary; source adapters already exist.
Reuse qc_binding_program.py and qc_binding_native_fixture.c plus the actual
native dedicated bootstrap/loader/interpreter. GPU work remains stopped. No
second VM, dispatcher, packet protocol, parser or resource owner is proposed.

Main verified pr_ext.c4417 named-call forwarding, saved argc and shared
PR_InvokeBuiltin at6869: ordinary QC uses the native executor, negative targets
reuse native core/lazy VM-specific binding; zero/missing targets are no-ops.
Inherited reference pr_cmds.c6077 has unconditional executor entry, while native
vkQuake executor assumes a QC body. The existing narrow target-aware adapter
and its prior named-call plan resolve that incompatibility. Message readers
at5927 reuse actual MSG_Read* and client protocol flags, with SSQC unavailable.
isfunction remains name existence, including unbound/unsupported declarations.

Implementation slice before code: optional --calls assembler and -calls native
fixture mode; original core/resources/files modes remain identical. Extend the
existing function-record helper with optional parameter sizes/defaults only
if needed; do not copy a second function assembler. Append two-argument nested
QC callback and seven-argument summation body with genuine native locals and
parameter metadata. Native locals start with known sentinels and must restore
after execution. Named builtin core fabs, lazy min, nested call, seven forwarded
arguments and exact return values/argc are observed in both VMs. Missing/zero/
no-argument targets preserve a primed return; isfunction distinguishes actual
name existence from invocation permissions.

CSQC reads a real native-writer byte/char/short/long/coord/angle/string/float
payload and leaves an unread trailing sentinel for a subsequent native reader.
Full CL_ParseServerMessage/CSQC_Parse_Event dispatch is a separate boundary.
Exercise native legacy and float-coordinate/short-angle flags, exact signed/
fractional values and cursor conservation; a further native QC read observes
EOF and badread. Save/restore net_message/cursor/error/flags; do not assign
reader results or runtime capabilities. Packet bytes are test inputs, not a
new transport. Repeated CSQC load and opposite VM preservation stay native.

Separate dedicated negative modes invoke SSQC readbyte through callfunction and
an out-of-range numeric named target. Native PR_RunError/Host_Error/Sys_Error
must reject with the expected target diagnostic and exit1; no custom exception
trap or GUI state masquerading as dedicated state. These exits do not prove
post-error client-VM/GUI resource recovery. No sockets/window/GPU/microphone.

Coding contract: one explicit Luna/xhigh worker, writes only the two existing
test files, <=220 Python/<=140 C additions. Main independently prepares private
profiles/build/inputs/reference evidence; no overlapping edits. Worker is not
alone, must retain others' edits; no production/docs/runtime/build/branch/commit
work. Return scope_done/files/source checks/assumptions/risks/follow-up <=400words;
stop/report missing evidence or expansion. Main reviews source, runs affected
strict CPU-only native checks after implementation and gets verified local
Astra review for any discovered substantive production correction/claim gaps.
No512-builtin suite; token/entity/surface/HUD/error-recovery remain distinct F03
boundaries and rendered consumers remain F05. No whole-owner acceptance here.

Final review strengthened the same slice with distinct nested counts, per-slot
QC argument witnesses and a valid zero-forwarded QC target. Native owner code
remained unchanged; final implementation stays within220Python/140C additions.
[Actual outcomes and limits](qc-calls-current-2.0-results.md),
[verified Astra dispositions](qc-calls-final-2.0-review-brief.md#final-disposition).
