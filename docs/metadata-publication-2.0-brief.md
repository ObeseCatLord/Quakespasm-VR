# C02 metadata publication decision brief

2026-10-01. Mostly-worked main brief for local Astra xhigh. Final checklist C02,
NET-021: publish initial serverinfo/userinfo and clear retired-slot custom data.
This is design assessment before a committed implementation plan, not code or
executable acceptance. Writable quakespasm-2.0 only, known2.0; no branch checks.

## Verified source/environment facts

| Fact and verification | Boundary |
| --- | --- |
| Main read sv_main.c SV_SendServerinfo and pinned QSS-M03a498 sv_main.c2276 | Current initial serverinfo omits the QSS-M //fullserverinfo quoted snapshot. Current serverinfo retry truncates model/sound precaches on overflow; append timing matters. |
| Main read host_cmd.c Send_Spawn_Info and QSS-M9802 | Current sends native name/colors/frags only for knowntoqc slots after SZ_Clear. QSS-M sends //fui snapshots for every slot for PEXT2_PREDINFO after QC spawn, then native fields. |
| Main read host.c SV_DropClient and QSS-M1437 | Current clears native name/colors/frags but custom metadata survives in peer scoreboards. Adapt capability gating to the recipient, not QSS-M's departing-client predicate. QC disconnect must still see the original userinfo before retirement. |
| Main read server.h/quakedef.h/net.h | Stores8192 bytes, MAX_SCOREBOARD16, NET_MAXMESSAGE64000. Whole full-userinfo table can exceed one reliable message. Cannot blindly append16 near-full snapshots to Send_Spawn_Info. |
| Main read cl_parse.c svc_stufftext and cmd.c tokenizer | Fullserverinfo already has an8192-capable bounded reader/tokenizer exception. fui currently retains the generic2047-byte command ceiling, incompatible with its8192-byte store. |
| Main read cl_main.c handlers | FullUserinfo/UserinfoUpdate only check slot<maxclients; negative slots and argc are not validated, and strncpy replacement lacks explicit termination. Existing owners can be repaired narrowly. |
| Main read SV_UpdateInfo cl_main.c and Cvar_SetQuick cvar.c | Existing incremental ui/svi messages remain. Underscore keys are deliberately private in SV_UpdateInfo. Full snapshots must not publish them. Info_SetKey only rejects backslash, not quotes/newlines; current quoted commands cannot represent such fields faithfully. |
| Main read sv_main.c native avatar publication | Existing avatar table marks16 dirty bits and flushes current model-owned descriptors when reliable room is available. Existing message lifecycle and dirty-set idiom are reusable; copying all strings into a second queue is unnecessary. |
| Main read native prespawn stages/server loop | Before spawned, PRESPAWN_DONE suppresses ordinary messages and sends only periodic nop. A pending metadata flush must not silently rewrite stage ownership or bypass this guard accidentally. Spawn SZ_Clear can erase queued optional publication; rebuild at spawn like native avatar table. |

Readonly pins: primary51b452c0; QSS-M03a498; vkQuake4bc898. Game assets stay in
quakespasm_straight and are irrelevant to this design. User-owned migration-2.0.md
is untouched. Active Luna renderer worker owns gl_rmain/glquake/gl_vidsdl/r_brush/
r_sprite/stereo.inc only; no metadata edits run during this review. Tests/builds/
compiler/lint/probes/fixtures/game runs are forbidden until full implementation.

## Open decisions and main lean

1. **Bounded publication using native reliable owner.** Prefer serverinfo-pending
   flag plus16 userinfo dirty bits on client_t, mirroring existing avatar retry
   semantics, serialized from current existing stores only when an entire command
   fits. Mark initial serverinfo on signon, and rebuild required snapshots after
   spawn SZ_Clear. Retired slot's current inactive snapshot is empty; reuse sends
   current new occupant data if retirement was overtaken, avoiding stale buffers.
   Source/helper may live in sv_main.c with thin calls from host_cmd.c/host.c and
   server.h declarations. No second metadata registry, queue or protocol.
   Challenge whether reusing prespawn cursor stages is actually simpler; that
   alternative must also survive QC spawn updates and SZ_Clear. Reject unchecked
   va()/monolithic16-snapshot append and silent truncation/forever-pending commands.
2. **Capability/order.** Preserve native name/color/frags for all peers. Userinfo
   snapshots use recipient PEXT2_PREDINFO like QSS-M. Initial serverinfo must clear
   old state even when empty. Decide exact scheduling around initial signon,
   spawn and idle prespawn, and handling of metadata not fitting even an empty
   NQ8192 message. Prefer reuse of the existing reliable sender and stage owner,
   not expanding packet sizes or inventing chunks unless source proves necessary.
3. **Representable/public metadata.** Native Info_Enumerate/Info_SetKey can build
   a temporary public snapshot, excluding underscore keys. Quoted protocol cannot
   express literal quotes/newlines. Prefer a narrow serialization/validation
   boundary consistent across existing incremental ui/svi and full snapshots,
   preserving local stores and normal printable metadata; do not add another
   command syntax or global parser escaping policy. Exact treatment of invalid
   fields is still unknown; verify callers/reference before recommending rejection,
   omission or normalized storage. Do not weaken privacy just to copy QSS-M.
4. **Receiver limits.** Extend only the explicit fui command's bounded reader
   allowance alongside fullserverinfo. Validate argc, nonnegative/range slot and
   terminated replacement in the actual handlers; keep all other command limits
   and current command/temporary-string owners. No general tokenizer rewrite.

## Scale and requested review

Anticipated narrow family: server.h, sv_main.c, host_cmd.c, host.c, cl_main.c,
cl_parse.c and possibly cvar.c/common helpers if a demonstrated shared serializer
requires them. No production edits yet. Reopen if it becomes a second metadata
policy/protocol owner. Main owns architecture, final integration and user scope.

Read this brief, verify actual native/reference callers before critique. Rank
decisions and choose the smallest end-to-end design; challenge the pending-state
lean and seek deletion/simplification. Output<=1300words with prioritized
disposition, exact scheduling/capacity/privacy/receiver seams, unknowns and eventual
observable acceptance. Read-only, no tests/probes/telemetry/nested agents/branch
checks/assets edits. If broader than the budget, name the exact unclosed question.
Final software acceptance includes initial/empty/near-limit metadata,16 slots,
mixed native/PREDINFO peers, spawn clearing, mid-signon updates, drop/reuse,
private/invalid keys, malformed commands and current movement serverinfo consumers.
