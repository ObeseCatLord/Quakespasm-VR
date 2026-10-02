# Loaded command dispatch: final qualification plan

2026-10-02. Copy/player dispositions implemented/qualified in77875fd8; source has limited writable space again. Reuse existing licensed-program assembler/native fixture and unchanged CL_LoadCSProgs host adapter. No new command registry, parser, VM-generation service, interception framework or protocol. Existing source adaptation: docs/qc-command-dispatch-2.0-plan.md (21net-line native/QSS-M adapter). Main inspected current cmd.c849–946,1011–1101, native Cbuf_Execute166–235, host.c993 client admission and PF_cl_registercommand.

Separate trailing commands=False / --commands / -commands and QC_COMMAND_NATIVE_HOST_FIXTURE; preserve original six program prefixes/version/CRC, entity width and all13previous modes byte-for-byte. Append actual numeric registercommand352 plus needed existing token/cvar/string builtins. No-op CSQC_Ent_Update admits actual loader. CSQC_ConsoleCommand uses real one-string parameter metadata (parm_start/locals_count/parm_sizes), not synthetic OFS_RETURN assignments or direct C invocation. Loaded QC stores full text, tokenizes/observes quoted arguments, produces an observable private non-archived cvar effect and returns configured accept/decline. Native temporary string/zone lifetimes remain owners.

Loaded client QC registers unique names with numeric builtin. Native Cmd_FindCommand observes dynamic independently-owned name, NULL function, src_command and dedup identity. Re-register same name and an existing private native handler name through QC: preserve original record/function. No new registry teardown policy; remove only fixture-created records at final retirement through native Cmd_RemoveCommand.

Actual Cmd_ExecuteString invokes registered handler with NULL, server or client VM previously active; verify previous VM restored, exact supplied text/quotes and private cvar effect. Accept and decline both invoke callback; decline produces controlled diagnostic and does not fall through to a same-name alias/native handler. Missing callback/program/unload instead produces fallback, leaving native/cvar effects unchanged. Do not interpret Cmd_ExecuteString true for a matched but denied src_client record as evidence the handler ran.

Prepare actual host_client from native server clients for direct src_client checks. Direct src_client/src_server must execute neither client callback nor source-ineligible native handler; server/command/native handler class control cases retain current native semantics. Actual Cbuf_AddText/Cbuf_Execute uses src_command and therefore can reach registered client callbacks, including buffered stufftext semantics. Native alias and cvar paths retain quoted-argument behavior; direct remote sources never get alias/cvar fallback. Do not claim human-only origin.

Two actual client program loads/clears preserve the same linked server/program witness. Stale independently owned registration survives client clear and gets controlled fallback; later real client load reaches new callback without renewed registration. Repeat with renewed registration and actual server replacement. Private native handler and native cvar/alias controls verify unchanged ordinary desktop owners; clean only fixture-created inputs, preserving the global registry/buffer. Native error unwind is a separate boundary, not this fixture's proof.

Actual registered command consumption/cvar mutation and program/owned-name retirement supplement callback counts; no sole acceptance by counters/mocks. Final private CPU run must have assertion-enabled current native object recipe, actual dedicated loader, no socket/audio/window/Vulkan/OpenXR acquisition, all stage exits0 and completion marker. Senior review uses current source and recorded receipts; final integration fresh shipping target required. Not implementation or acceptance yet.


Ownership: one Luna/xhigh coding worker owns only tests/qc_binding_program.py
and tests/qc_binding_native_fixture.c, <=160Python/<=350C added lines. Main owns
this plan, README/private profiles/review/integration. No production edits or
new host adapter. Atomic writes after checking16MB free; no build/test/runtime/
branch actions or nested delegation. Report missing evidence/cap overflow rather
than reducing cases or broadening. Other worker owns only Windows project files;
no overlap. Main checks complete source before final CPU qualification and local
bounded Astra review. Production Windows/ARM/artifact gates remain distinct.

Alias cleanup uses actual `unalias` dispatch (Cmd_Unalias_f), not manual linked
list access. The one private non-archived fixture cvar has static process lifetime;
restore its prior value, do not invent cvar deletion. Track/remove only newly
registered private command records using Cmd_RemoveCommand. Actual parameter
metadata uses existing assembler add_function helper, like named-call QC functions.
