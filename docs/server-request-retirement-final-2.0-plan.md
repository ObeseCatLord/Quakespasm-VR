# Native client request boundaries: final implementation plan

2026-10-02. Confirmed D03/D04 under existing F01/F03, following the one
[consolidated local Astra review](consolidated-issues-senior-2.0-review.md).
The full remaining list is now frozen; implementation first, final tests after.
Only production write set Quake/sv_user.c. No protocol/VM/renderer/lifetime
manager, new entity generation field, general Debug relaxation or generic MSG
rewrite. Main/reference/baseline/assets and user migration document read-only.

## Minimal reuse versus replacement

D03: native entity decoding already bounds number to current num_edicts and
maps outside range to world0. But Debug EDICT_TO_PROG rejects an in-range freed
entity during network decoding, before lookup even of an absent handler.
Pinned MAIN sv_user.c/progs.h and QSS-M preserve numbered byte-offset references.
Reuse native pr_edict.c saved-reference offset conversion locally. Do not revive
free entities or disable live-entity assertions at other owners. All other type
parsing, cursor/badread checks, handler lookup, argc and requester lifetime stay.

D04: native string command may run SV_ParseClientCommand or normal native/QC
command dispatch. A callback may drop requester, and spawnclient may immediately
reuse its slot as a bot. Reader's next active check otherwise false-double-drops,
or accepts trailing old bytes as the replacement. Snapshot the original client/
socket around dispatch, restore host_client and apply the already-existing
clcfte_qcrequest retirement guard. A retired/replaced requester returns true from
the reader, ending this message without another SV_RunClients drop. Preserve
normal controls, spawn/prespawn/begin/pext exclusions and capability handling.

Adjacent D04 admission: MSG_ReadString returns a terminated prefix on EOF and
marks msg_badread. Reject false immediately after that read before capability,
QC or normal dispatch. Valid terminated strings unchanged. Do not change native
terminated-overlong-string truncation or client reader capacities in this slice.

Expected change below25lines in one file. A general SV_DropClient inactive guard,
new queue/state machine/connection-generation manager or entity revival would
change working adjacent behavior and fail the minimal adapter comparison.
Reopen design if evidence demands them. No reproduced-runtime claim yet; bugs
are verified native source paths and reference incompatibility.

## Ownership / final verification

One Luna/xhigh implementation worker owns only sv_user.c; main reviews its full
patch against these references and checks non-overlap. No tests/builds during
implementation. Once all confirmed implementation issues in the consolidated
queue are resolved, final verification starts in four combined groups listed
in the senior dispositions. Reuse existing native QC/network fixtures/loaders.

This repair's meaningful final checks: Debug/Release freed bounded entity with
present/absent handler, outside-range world fallback, malformed/truncated typed
requests and following opcode alignment; terminated native/QC control, EOF-prefix
refusal before effects, self-drop exact-once disconnect/accounting, immediate
slot replacement with no inherited trailing command or movement. Observe actual
native parser/loaded callbacks and subsequent message/client state; don't assign
returned parser results or supply a replacement lifetime policy. D01/D02 final
mixed-peer evidence records timing/input/velocity/obstruction and each process's
terminal status/stack if an abort recurs. No claim these defects explain them.

Production change requires affected Windows Debug/Release, Linux x86-64 and
native ARM artifact/source refresh under A01 after final implementation. Reuse
unchanged dependencies/notices/staging semantics. Physical headset/gaze/user
live tests and performance measurement remain excluded.
