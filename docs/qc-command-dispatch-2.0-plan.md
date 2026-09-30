# Registered CSQC commands at the native dispatch boundary

2026-09-30. Repair demonstrated source admission and NULL dispatch defects;
reuse the existing client VM callback for registered game commands. Plan
precedes production edits. Proposed write scope only Quake/cmd.c,<=40 net lines.
Do not alter the command registry, tokenization, command-source policy, VM,
transport or native command handlers. No tests/builds/probes until all
implementation is finished; main/master and dirty migration-2.0.md untouched.

## Verified native and reference behavior

Current2.0 pr_ext.c:5736 registers a CSQC command by calling Cmd_AddCommand with
NULL; the native entry description at6328 says it invokes CSQC_ConsoleCommand.
cmd.c:909 calls the stored function unconditionally. Registering and invoking
an ordinary fresh name therefore dereferences NULL. The source-restriction
if/else chain also leaves that call reachable after logging an inadmissible
src_client command. These are demonstrated control-flow defects, not evidence
that the registry or source policy requires replacement.

Primary51b452c0 cmd.c:1049–1060 keeps its existing source checks, native function
call and controlled NULL fallback in one mutually exclusive chain. Copy this
guard/control flow. The primary fallback alone does not invoke the callback.
Native progs.h:247 already stores CSQC_ConsoleCommand; native PR_ClearProgs
clears the client program/extfuncs through existing VM lifetime ownership.
QSS-M03a498aa cmd.c:1625–1642 supplies reusable previous-VM capture, NULL switch,
client VM switch, PR_MakeTempString(text), callback, return read and restoration.
Native PR_SwitchQCVM requires the intervening NULL switch. Reuse this pattern
only for an admitted registered NULL-function src_command-class command.

## Minimal adapter and deliberate boundaries

1. Native/source-denied/function/NULL cases remain mutually exclusive. Preserve
   native server-only skipping and remote-client admission; a rejected source
   must execute neither the native handler nor client QC.
2. For admitted src_command dispatch of src_command-class entries whose handler
   is NULL, invoke the existing
   CSQC_ConsoleCommand only with a live client program and callback. Pass the
   complete dispatched command line within native1024-byte limits. Capture the
   callback return before switching away. Restore the previous VM after an ordinary
   callback return. Missing or declining game code gets a controlled diagnostic.
3. Keep native command execution, alias/cvar fallback and unknown-command
   behavior. Do not copy QSS-M menu or broad native-command interception.
   No new command generation table or registration lifetime owner is justified.

The gate is a dispatch-source restriction, not a human-only guarantee: native
server stufftext is queued through Cbuf_AddText and later uses src_command.
Preserve that established buffered-command behavior. Direct src_client and
src_server dispatch do not enter the client callback.

Command names have independently owned native storage and survive client VM
unload/reload. PR_ClearProgs clears program/callback fields, not command records;
PR_ShutdownExtensions does not remove them. A stale name therefore gets the
controlled fallback while no program exists, and may reach a subsequently loaded
program's callback without renewed registration. Preserve this existing lifetime;
do not claim registration belongs exclusively to the current client program.

A guard-only change matches the primary's graceful fallback but leaves the
native documented callback utility absent. The narrow QSS-M reuse supplies it
without a parallel implementation. Full QSS-M interception would change
adjacent working native dispatch, so it is deliberately outside this repair.
Reopen before another production file, parser changes or new registry state.

## Review and final acceptance

The initial requested-Astra source audit verified the NULL defect and existing
owned runtime command-name storage. Main confirmed both dispatch defects and
the actual primary/QSS-M flows. A focused requested-Astra advisory reviewed
callback admission, VM restoration and current native teardown before coding.
Its terminal advisory recommends adopting the narrow adapter with the following
main dispositions, spot-checked against the actual source/reference flows:

| Finding / recommendation | Main disposition |
| --- | --- |
| NULL guard alone leaves source-denied native dispatch reachable. | Adopt complete mutually exclusive primary control flow. |
| Callback already has native storage/loading; QSS-M supplies VM pattern. | Adopt NULL-handler, src_command dispatch and registration-class gates; require program/callback; capture return before VM restoration. |
| Sbar_CSQCCommand is not an equivalent generic helper. | Adapt at dispatch boundary: preserve full argument text, active-VM restoration and independence from HUD style; do not rewrite score callbacks. |
| VM teardown does not remove command registrations. | Correct lifetime description above and cover stale-name reload in final checks; no new generation manager. |
| Buffered stufftext uses src_command; original permission wording was too broad. | Correct source description above; keep native buffer/transport behavior. |
| Native command/temp-string buffers bound line length. | Preserve the dispatched line within native limits, not unlimited input. |

Effective reviewer settings are unexposed; this is requested-Astra source
advisory, not certified skill/model/runtime acceptance.

## Source integration checkpoint

The21-net-line cmd.c adapter is implemented. Main reviewed the complete diff;
the final requested-Astra source advisory accepted it with no remaining P1/P2
finding. Source-denied, native-handler, registered callback and fallback cases
are mutually exclusive. Only admitted NULL-handler src_command records enter
the loaded client callback; the previous VM and return-value sequencing match
the adopted QSS-M pattern. Native command and registration lifetime owners
remain. Scoped whitespace checks passed. No executable checks were performed;
the acceptance below remains end-of-implementation work.

## Final software acceptance

After full implementation, Linux/ARM checks exercise actual QC registration and
execution, exact command text/arguments, callback accept/decline, absent/unloaded
client program, reload/disconnect with renewed and stale names, duplicate/native-name registration, restored
previous VM and source-denied commands. Ordinary native handlers, aliases and
cvars retain behavior. VM-error recovery remains native Host_Error ownership;
do not add a second exception/state owner. Final checks must not rely only on
handler counts or mocked callback counters.
