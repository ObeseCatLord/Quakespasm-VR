# Inherited co-op admin commands through native owners

2026-09-30. Full vkQuake migration remains the parent goal. This plan covers
the inherited console/admin controls, shared by desktop and VR clients. No
renderer, protocol, inventory schema, remote authorization mechanism or mod
whitelist is added. Production edits stay on2.0; main and the user-owned
`migration-2.0.md` remain untouched. No builds/tests/probes until implementation
is complete. Quad views remain excluded from the parent scope.

## Behavioral reference and current evidence

Reference: read-only `quakespasm-openvr`, pin
`51b452c018273647dcf94f4628a370267ff8fa91`, `Quake/host_cmd.c:3057–3445` and
`:3484–3536`; `Quake/cl_main.c:909–1018`. The four direct admin commands
are `sv_giveall`, `sv_givekeys`, `sv_god` and `sv_noclip`. They select the
first active player when no target is provided, case-insensitive player names,
one-based `# slot`, or `all`. Cheat commands default to on with numeric zero
turning them off. Key grants accept silver/key1, gold/key2, all/both/keys.
Give-all prefers the mod's zero-argument `GiveAllCommand`; otherwise it grants
stock/mission-pack and declared weapon ownership plus reference ammo floors.
Keys use the existing mod-aware co-op inventory owner and canonical team state.

| Current fact | Evidence and consequence |
| --- | --- |
| [verified: source] The four direct commands and paired `sv_reconnect_game` / `qs_reconnect_game` are absent in2.0. | Registrations and handlers were searched directly, not inferred from inventory statuses. This is missing behavior, not an obsolete cvar name. |
| [verified: source] Native `Host_Give_f` forwards console requests, and `give all` sets impulse9 for the requesting client. | `host_cmd.c:3920`. Preserve it; it cannot replace selected-client dedicated admin grants. |
| [verified: source] Inventory/key owners are already ported. | `server.h:544,553–564`, `world.c:730,1162,1624`, `sv_phys.c:858`: `SV_DeclaredWeaponBits`, `SV_CoopGiveKeys`, `SV_CoopSharedRebuildGrantedKeys`, `SV_CoopRespawnRefreshClientInventory`. Reuse them without copying their schemas into commands. |
| [verified: source] Named field lookup exists internally, but the public host header exposes native field-offset lookup. | `pr_edict.c:452`, `progs.h:211–212`. Adapt reference calls to `GetEdictFieldValue(ent, ED_FindFieldOffset(name))`; do not expand the public VM API. Optional fallback fields must have compatible float/integer meanings. |
| [verified: source] Native command dispatch logs an unexpected src_client command but can still invoke its handler. | `cmd.c:888–900`. The new admin handlers must reject any `cmd_source != src_command` before mutation. Local console and established RCON use that owner. Do not broaden remote-player permissions or rewrite the dispatcher in this slice. With no local server, print a refusal instead of forwarding admin commands as ordinary player commands. |
| [verified: source] Native VM switch refuses switching between two non-null VMs. | `pr_edict.c:1771`. Preserve old VM, detach before installing server VM, restore after grants. The copied reference restores host_client/sv_player but leaves QC self/time modified; isolate the new callback and restore those globals on ordinary return. |
| [verified: source] Native PR_ExecuteProgram cannot execute an arbitrary builtin declaration safely. | `pr_exec.c:315–336`. Call a valid zero-argument QC body only; unsupported callback declarations use the reference fallback. Keep existing Host_Error unwinding, not a new recovery owner. |
| [verified: source] Auto reconnect and asynchronous connect owners already exist. | `cl_main.c:439–623`, `host.c:1158`, `common.c:3561`. The later paired game-reconnect command must extend these owners rather than copying the reference's parallel blocking connector/retry state. Exact cancellation/lifetime integration needs a separate verified stage2 brief before production. |
| [unknown: execution] Actual dedicated/listen/RCON behavior, callback-driven client retirement and Linux/ARM builds. | Source acceptance is not an end-to-end completion claim; final consolidated software checks remain required. Live multiplayer trials are the user's deferred work. |

## Small adapter versus replacement

Adopt the copied reference target parser, grants, key-kind parser and four
handlers in `Quake/host_cmd.c`, with narrow native lookup/VM/authorization
adaptations. No new persistent state. Existing world/inventory/QC/command
owners remain authoritative. Replacing native give/cheats, copying key schemas,
adding a second inventory or broad command framework duplicates behavior
without a demonstrated incompatibility and is rejected.

Stage1 estimate: <=450 added production lines in `host_cmd.c` only. Prefer
straight reference reuse over a new abstraction. Reopen if changes require a
new inventory/callback/authorization owner or exceed that scope. This stage is
a direct command adapter, not an expensive renderer/network architecture fork;
bounded local Astra source review follows implementation.

## Stages and invariants

1. Implement four direct console/admin commands and register them through
   native `Cmd_AddCommand`. Every handler explicitly restricts command source.
   Reject inactive/free/missing client edicts. Keys retain the reference's
   spawned/alive requirements; give-all and cheats retain active-client scope.
   Targets and option positions retain the reference syntax. Preserve stock
   `give`, `god`, `noclip`, `notarget`, fly, input and prediction behavior.
2. For give-all, copy reference stock/mission-pack masks and ammo floors,
   including native declared weapons. Validate optional field types; do not
   reinterpret unknown schemas. Use a compatible mod `GiveAllCommand` body
   when present, with self/time and other scratch callback globals isolated.
   Save/restore host_client, sv_player and the active VM on ordinary return.
   After callbacks, refuse inventory refresh or follow-up mutation when the
   target client/edict has retired. Native QC Host_Error remains its existing
   fatal/unwind boundary. Key grants reuse their existing callback isolation
   and rebuild canonical team keys once after successful grants.
3. Plan and implement the paired game-reconnect controls, preserving reference
   delay/retry/timeout behavior through existing frame-boundary async connect,
   cancellation, game switch and server-notification owners. This remains in
   the full goal; completing stage1 must not hide it. Do not start a second
   retry state machine or change filesystem/server state inside packet parsing.
4. Final software acceptance after all implementation: Linux x86-64 and ARM
   builds; dedicated/listen selected/default/all targets; invalid/free/inactive
   targets and bad key kinds; explicit on/off cheats; stock/mission-pack and
   declared ownership; compatible QC callback versus fallback; client-command
   refusal; VM/global restoration; callback removal; team/respawn/late-join key
   preservation; desktop/VR crossplay. Stage2 additionally requires delayed
   reconnect, timeout/cancellation, malformed operands and game-switch/lifetime
   checks. User headset/performance/Windows checks remain deferred.

Main reviews the copied code against both references and the native boundaries,
records findings/disposition and commits narrowly. Source checks alone do not
close the broader co-op saves/late-join/network requirements.
