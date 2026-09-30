# Native host command source registration repair

2026-09-30. Before-code prerequisite for inherited multiplayer and QBJ3
callback integration. No builds or execution checks until implementation is done.

## Verified problem and reference

The native vkQuake host registration table uses Cmd_AddCommand for ordinary
player commands. Current cmd.h:113 classifies that macro as src_command.
Cmd_AddCommand2 stores the class unchanged; it does not automatically register
a client-class twin. The adopted source-denial guard in cmd.c:1030 therefore
rejects these entries before their native handler for a network src_client
command. sv_user.c dispatches clc_stringcmd through precisely that source.
This contradicts required signon, chat, identity and desktop/VR multiplayer.
It also disproves the earlier QBJ3 brief's claim that clientcommand(spawn) was
already a returning source route. Runtime execution remains unverified.

QSS-M03a498aa host_cmd.c:12134–12174 already registers the shared ordinary
player commands as src_client. Current Cmd_ExecuteString permits src_command
to execute that class, so the same handler serves console and remote players.
Handler permission/cheat/argument policy remains authoritative.

## Minimal adapter and ownership

Copy only the client source class at the existing host_cmd.c registration
owner for the19 shared commands: status, god, notarget, fly, name, noclip,
setpos, say, say_team, tell, color, kill, pause, spawn, begin, prespawn, kick,
ping and give. Preserve qcinterceptable=false: wholesale QSS-M command
interception is outside this slice. Existing setinfo/serverinfo, CSQC and co-op
action registrations already have the correct class and remain owned there.

Compare with weakening the source-denial guard or rewriting command dispatch:
those would expose unrelated console/server commands and duplicate policy.
The19 existing registrations reuse the actual donor/native owners and preserve
the mutually exclusive guard and NULL CSQC handling. No parser, transport,
handler or command registry replacement is needed. Only host_cmd.c is writable;
19 changed lines/zero net lines expected. Reopen for any additional handler,
dispatch or policy change.

## Integration and final acceptance

Main verifies all19 handlers against the existing source/permission boundaries
and QSS-M source classes, reviews the full diff and records the source checkpoint.
After registration repair, the ordinary src_client spawn route is available;
its active/pre-begin and saved-client branch is the QBJ3 plan's bounded nested
cancellation case. Do not use that consequence to rewrite native spawn.

At the end, Linux/ARM checks must exercise real signon through prespawn/spawn/
begin, name/color, ordinary and team chat, status/ping, kill/pause and permitted
versus denied cheat/kick/give paths, both desktop and VR. Console-only load/save/
quit/map/changelevel and server-only source restrictions remain enforced.
Source inspection alone does not certify multiplayer behavior.

## Source integration checkpoint

The19 registration replacements are source-integrated with zero net production
lines. Main reviewed the complete diff and native forwarding/source checks:
player identity/chat retain their handlers, spawn/begin/prespawn retain the
pre-begin guards, cheats/give/kick retain native deathmatch policy and pause
retains pausable. Cmd_ExecuteString still admits these from src_command, rejects
non-client classes from src_client and rejects them from src_server. QSS-M
source classes match all19; no interception flag or handler was changed. Scoped
git diff --check passes. No builds, runtime or software acceptance ran.
