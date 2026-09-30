# Current inherited co-op save and join owners

2026-09-30. Current-source checkpoint for COOP-010/011; no tests or builds.
This supersedes historical claims that the inherited loader or autosaves are
absent. It does not close the whole multiplayer restoration matrix. The
[save boundary design](migration-savegame-boundary.md) retains its chronology
and earlier evidence limits; [final Linux/ARM qualification](final-linux-arm-qualification-2.0-plan.md)
remains after all implementation. Co-op revival is excluded.

## Observed current source

| Requirement/owner | Direct source evidence and disposition |
| --- | --- |
| v5, inherited v6/v7 and native KEX v6 dialects | `host_cmd.c:2654–2698` uses `Savegame_ClassifyHeader` and bounded `Savegame_ValidateInheritedHeader` before nomonsters/skill/connection/game changes. Ambiguous or unsupported headers return without world mutation. `savegame_dialect.h:190–218,416–493` distinguishes the conflicting v6 layouts; it validates the fixed inherited prefix and opening globals brace, not later QC semantics. |
| Preserve native loader/game selection | `host_cmd.c:2709–2776` retains KEX game lookup/switch and reopens/reclassifies the original save afterward. Common globals/edict, precache and native fastload owners remain. Private single-player command history forces native reconnect instead of reusing stale fastload input. |
| Shared native writer and extended parms | `Host_SavegameWrite` writes ordinary v5 or inherited v7, preserving the16-parm inherited header and explicit per-client17–64 trailer. Checked temporary write/flush/close/rename publishes through the existing filesystem owner. Pending saved clients or active deferred spawn parms refuse writes; active clients require completed signon. |
| Dead-player inventory without gameplay during encoding | `sv_phys.c:1379–1400` projects typed cached/current inventory into a detached edict, preserves cached currentammo, and does not revive or call QC. `host_cmd.c:2140–2155` serializes that projection. Dead saved players run fresh entrance QC, then the existing exact inventory/expiry adapter; living saved payloads retain their original state. |
| Reserved player edicts and payload identity | `pr_edict.c:134,261,280,1023` excludes reserved edicts from ordinary allocation/free-list membership. `host_cmd.c:2466–2505` copies only QC payload plus serialized alpha into actual reserved edicts, retaining native link/retain/debug metadata. Parsed pending player bodies are freed/unlinked; empty/missing promised payloads reject at the existing load owner. |
| Identity and deferred joining | `host_cmd.c:2542–2566` matches current slot/name then other pending names, with unnamed/v6 or single-player slot fallback. `sv_main.c:3739–3777` stores per-connection deferred spawn-parm state. `Host_Spawn_f` selects living restore, dead restore or genuinely new defaults; defaults are read through named extended globals instead of assuming contiguous parm17–64. |
| Shared progression and restore completion | `host_cmd.c:3125–3148` merges all restored progression before signon can expose one player; normal/dead/new joins use `SV_CoopSharedApplyToJoiningClient`. Existing slot completion clears only consumed snapshots; save/autosave refusal persists while other snapshots or defaults remain pending. No inventory copy was added inside the parser. |
| Map lifetime and native respawn integration | `host.c:758–764` frees snapshot storage with server/VM lifetime. New QBJ3 saved-inventory callback cancellation returns from HostSpawn before alpha/frags/slot consumption/signon follow-up. Dead-changelevel retention belongs to existing `SV_SaveSpawnparms`/bounded `SV_CoopRespawnSetChangeParms`, not a save-only protocol. |

## Remaining evidence and implementation boundary

These concrete owners are present; a stale inventory label is not evidence
that another save parser, inventory schema or reconnect service should be
implemented. No new save implementation is authorized by this checkpoint.
Native/source parity still needs the full v5/KEX6/inherited6/7 round-trip matrix,
two named saved clients in reverse order, dead inventory with exact zero ammo,
empty reserved slot/projectile allocation, newcomer defaults before the last
restore, map/hub progression, pending cancellation and malformed-body graphical
recovery. Prior one-player/header/movement checks are narrower than those
requirements. Existing client-entrance QC callback lifetime beyond the new
saved-inventory cancellation guard is not certified here.

The separately scoped [autosave checkpoint/repair](coop-autosave-source-2.0-plan.md)
retains progress/rotation/native writes; final software qualification covers
publication failures and restorable checkpoints. Deliberate manual writes into
the inherited `coop_autoN` namespace share those slots; unrelated manual saves
are not overwritten. No general manual-name protection is claimed.
