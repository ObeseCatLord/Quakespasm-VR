# Multiplayer save migration boundary

The current vkQuake loader in `Quake/host_cmd.c` owns legacy version 5 and KEX
version 6 saves, including KEX mod selection, extended precaches, fastload, and
the existing QuakeC entity/global parser. `Quake/savegame_dialect.h` already
classifies inherited multiplayer versions 6 and 7 before game or mod mutation,
but `Host_Loadgame_f` still rejects them. The inherited product writes version
7 with a max-client count, per-slot identity, colors, frags and spawn parms;
version 6 has a related layout without names. Its load path retains pending
clients for reconnect and restores their entity snapshots at spawn. The
version-6 number is shared with KEX, so dispatch by number alone is unsafe.

## Decision before implementation

| Option | Reuse and incompatibility | State/policy cost | Smallest proof |
| --- | --- | --- | --- |
| Extend the existing `host_cmd.c` save/load owner with dialect-specific header and client-slot adapters | Reuse vkQuake file lookup, KEX handling, entity/global parser, and preflight classifier. Adapt only inherited 6/7 header, per-client snapshots, and reconnect state. | One parser and one pending-client owner; bounded additional slot state in the existing server. | In a disposable co-op game, save with two clients including a dead player, reload, reconnect in reverse order, and retain identity/inventory. Legacy 5 and KEX 6 still load. |
| Import the inherited save/load functions wholesale | Replaces working KEX/mod/fastload handling despite no demonstrated incompatibility in the common entity and global format. | Parallel parsing or broad replacement, duplicated game-switch and error policy. | Same proof, plus retesting every donor save path. |

Use the first option. Treat any conflict in the common entity/global parser as
an observed incompatibility to resolve at that boundary, not a reason to
replace the entire loader. Keep save publication atomic and preserve the
inherited rule that automatic saves must not discard clients pending from a
load. Validate the full inherited fixed header and first QuakeC block before
disconnecting or switching the current game. Recheck dialect and source file
after any mod switch. Do not infer a multiplayer header from a bare `6`.

The implementation is incomplete until inherited 5/6/7 and donor KEX 6
fixtures exercise both parsing and behavioral round trips, including dead and
disconnected slots, name matching, late joins, hub progression, and autosave
rotation. A header classifier or emitted file alone does not establish those
behaviors. This is COOP-010 through COOP-012 in the feature map.
