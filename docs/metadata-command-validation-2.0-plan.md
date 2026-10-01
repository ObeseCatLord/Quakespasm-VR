# C02: complete metadata commands at the existing receivers

2026-10-01. Independent before-code substep following the existing
[Astra metadata disposition](metadata-publication-2.0-review.md) and the
[receiver repair](metadata-receivers-2.0-plan.md). Recipient capacity/publication
remains unresolved; no sender/capability policy is chosen here. No tests/builds/
compiler/lint/probes/game execution until all required implementation is finished.

## Verified boundary and minimal adaptation

Main read cmd.c:766/780: Cmd_Args retains the original raw argument suffix.
Cmd_TokenizeStringBuffer stops at a newline outside quoted tokens and delegates
to COM_ParseExBuffer. Main read common.c:1859: the existing span variant also
provides the raw token start and end without changing parser semantics. A quoted
token currently succeeds at NUL even without a closing quote. Exact argc alone
therefore does not reject unterminated quoted replacements; text after an
outside-quote newline may also be unexamined by the handler.

Reuse COM_ParseExBufferSpan in one private metadata helper. Reparse only the
expected current arguments, compare them with Cmd_Argv, require each token that
starts with a quote to have a distinct closing quote (end-start>=2, end[-1]=='"'),
then accept only whitespace after the last argument. Native quoted LF remains
valid. Preserve native unquoted tokens, slot parsing and dispatch; no global
tokenizer policy change, new grammar/escaping or cmd_source restriction. One
bounded SERVER_INFO_STRING_SIZE scratch buffer is sufficient for all four
handlers. Existing COM span parser and argument storage stay the owners.

Main read cl_parse.c:3478–3492: the bounded svc_stufftext reader already detects
actual truncation and allows large fullserverinfo only. Its buffer also has room
for the shorter //fui prefix, two-digit scoreboard slot, full8191-byte userinfo,
quotes/newline/NUL. Add only //fui to the large-command exception; preserve the
generic2047-byte ceiling for other commands and existing truncation rejection.
The handler still validates the slot, argument count and complete raw command.

## Write/size contract

Only Quake/cl_main.c and Quake/cl_parse.c. Add the private complete-command
helper before the four metadata handlers; call it after their arity checks and
before any store/scoreboard/PM mutation. Retain the integrated bounded decimal
slot and terminated full-userinfo replacement. Extend the explicit reader
exception and correct its explanatory comment, without resizing global readers.

Expected35–60 changed lines; reopen above80 or any new common/cmd API/sender
policy. Source review and scoped diff checking only. Main owns integration.
Final software checks must cover missing quotes including a lone opening quote,
extra/newline-separated commands, empty/valid quoted LF, native unquoted tokens,
bad slots, near-limit fullserverinfo/fui, truncation and unchanged other-command
limits. This slice does not close publication/privacy/envelope C02 requirements.
