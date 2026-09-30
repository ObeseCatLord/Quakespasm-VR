# Explicit post-config overrides through the native command buffer

2026-09-30. BASE-002 includes inherited postcfg ordering. The user waived old
saved-setting migration, not explicit command-line override behavior. Plan
before code: preserve -postcfg startup/game-change functionality using copied
primary queue control flow and existing native file/command owners.

## Verified reference and current gap

Primary51b452c0 cmd.c:359–484 reads each repeated -postcfg filename. Absolute
OS paths are read directly; relative paths try the installation base root
before game search. Its native command-buffer generation suppresses duplicate
execution and invalidates old queued work on a game change. Filenames are
inserted in reverse so their text executes in command-line order. Primary
host.c:1777/1788 queues after startup configs for client/dedicated; common.c:3218
requeues after game configs. In both client paths the primary first queues
vid_unlock, then postcfg, then inherited binding/default adjustments.

Current native host.c:1414/1423 queues quake.rc or dedicated autoexec/stuffcmds;
common.c:3541 queues game quake.rc. No postcfg queue/registration exists in
the inspected source. COM_LoadConfigFile owns ordinary global/game config
composition, which is retained. -postcfg is a separate explicit later override,
not justification to replace that loader or reproduce legacy defaults.

Destination COM_LoadMallocFile_TextMode_OSPath at common.c:2812 already reads
OS files, including native text-mode handling. Despite its historical name,
it returns Mem_AllocNonZero storage; COM_LoadFile also returns native memory.
Both must be released with Mem_Free. Do not copy the primary's hunk/malloc
ownership branches or introduce another OS loader. Cbuf_InsertText already
appends a newline and owns inserted text after copying.

## Minimal adapter and behavior

Copy the reference's bounded filename scan, reverse insertion and queued
generation check into cmd.c; use existing COM_LoadMallocFile_TextMode_OSPath,
COM_LoadFile and Mem_Free. Declare the two queue APIs in cmd.h and call them
at current host/common boundaries. Expected production write scope only
Quake/cmd.c, Quake/cmd.h, Quake/host.c, Quake/common.c,<=150 net lines. Reopen
before loader/state-machine replacement or material growth.

Use unsigned generation values with defined wrap and a matching bounded
decimal parser for the internally generated command. No new timer, callback,
per-mod registry or config replay service. Match primary malformed flag rules:
missing filename or following +/-option prints a diagnostic and is skipped.
Refuse truncated OS paths rather than opening a different path. Absolute read
failure does not become a game-search fallback. Relative OS failure may use
ordinary COM_LoadFile search. Missing files report failure and leave the
remaining command buffer/configs intact.

Queue after existing startup config work for client and dedicated modes;
client startup/game changes must queue vid_unlock before postcfg and the
existing client binding adapter after it, matching the primary. Main draft
review directly verified native VID_Unlock(gl_vidsdl.c:854) calls VID_SyncCvars,
and VID_Restart_f ignores requests while locked. A draft inserting postcfg
before unlock loses explicit video overrides. Reorder only these boundary
queue entries; do not add conditional duplicate queues or a video replay owner.
On game change supersede prior pending generation after quake.rc/vid_unlock.
Do not alter ordinary exec/config precedence,
autoexec, stuffcmds, donor video policy or behavior with no -postcfg. Repeated
queued dispatch for the same generation is consumed once; game changes get a
new generation. Queued scripts remain ordinary native script text, subject to
native command-buffer limits. A public/internal command spelling alone does
not establish human-only provenance; native source gates/buffering remain.

Keeping only ordinary exec loses declared last-wins startup overrides and
game-change reapplication. Replacing the config loader duplicates existing
composition and cannot improve this narrow behavior. Reference queue reuse
is the smaller complete adapter.

## Final acceptance after full implementation

Linux/ARM software checks cover no-option native behavior; client and dedicated
startup; one/repeated -postcfg with conflicting ordinary config values; absolute,
relative root and packaged/game-search files; CRLF/empty/missing files; malformed
flags and long paths; command-line execution order; nested exec/wait; repeated
same-generation dispatch; game changes before old queued dispatch; reload and
explicit mod launch. Use copied fixture configs, not user runtime files. Final
source review must check actual allocator pairing, newline/queue ordering and
generation handling. No builds/tests/probes/fixtures/benchmarks before full
implementation; main/master and dirty migration-2.0.md untouched.
