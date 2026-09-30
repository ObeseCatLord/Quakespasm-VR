# Game reconnect commands on the existing async connection owner

2026-09-30. Stage2 of the [inherited admin plan](coop-admin-commands-2.0-plan.md),
within the full vkQuake migration goal. Solo maintainer; reuse existing owners,
no new connector, transport, background job or second retry state machine.
Production stays on2.0. No builds/tests/probes until all implementation ends;
Windows and live multiplayer/headset/performance checks remain deferred.

## Reference behavior and verified environment

The read-only primary reference is `quakespasm-openvr` pin
`51b452c018273647dcf94f4628a370267ff8fa91`:
`host_cmd.c:3447–3536`, `cl_main.c:195–275,599–639,909–1018` and
`common.c:2969–2996`. The server command announces
`qs_reconnect_game <game> <server> [delay] [retry] [timeout]` to clients;
the external wrapper restarts the server, not the engine command. Defaults
are8/2/120 seconds, clamps delay0..60, retry0.5..15, timeout at least delay+retry
and at most300. The client validates an installed single game directory,
disconnects, switches game at a frame boundary, waits for configs and retries
connections while remaining responsive. Manual connect/disconnect/game cancels.

| Fact | Evidence / boundary |
| --- | --- |
| [verified: source] Current2.0 already has one CL reconnect state owner. | `cl_main.c:75–99,439–623`: idle/wait_config/connecting/wait_signon; per-phase deadlines, endpoint/mod/legacy layout. One-shot automatic server-game switch and catalogue resume use it. No timed repeated-attempt public command exists. |
| [verified: source] Native frame loop calls it after Cbuf_Execute and before NET_Poll. | `host.c:1155–1162`. Reuse this boundary for the timed game switch and attempts. No new frame callback is needed. |
| [verified: source] Ordinary stuffed commands enter Cbuf_AddText; only double-slash commands execute src_server immediately. | `cl_parse.c:3474–3515`, `cmd.c:888–900`. Use ordinary native Cmd_AddCommand for the client command, explicit src_command guard, not immediate packet-safe registration. Explicit admin guard on server sender too. |
| [verified: source] Native COM_SwitchGame already owns renderer/task/model/FS/config transition and queues quake.rc plus vid_unlock. | `common.c:3446–3512`. It does not cancel reconnect itself; COM_Game_f does. Do not bypass model/task retirement or rewrite game switching. Wait for cmd_text to drain before connection. |
| [verified: source] CL_Disconnect cancels the async datagram attempt but intentionally leaves CL reconnect policy alive. | `cl_main.c:325–390`. CL_Disconnect_f, CL_EstablishConnection and COM_Game_f cancel through CL_CancelAutoReconnect. Preserve this distinction for timed retries after socket failure or server disconnect. |
| [verified: source] Datagram Start/Frame/Cancel already owns socket resolution/connection state. | `net_dgrm.c:2509–2555`. Remote retries reuse these APIs. DNS stays native synchronous resolution; no new blocking connection loop. Existing local connection path remains for local endpoints. |
| [verified: source] Primary GameDirExists examines the mount owner's roots. | Reference `common.c:2969–2996`. Native2.0 mounting uses com_basedirs/com_numbasedirs; use those actual roots and checked formatting rather than importing primary's different root state or rebuilding modlist on each command. |
| [verified: source] Native server NET_SendToAll changes host_client while sending. | `net_main.c:730–802`. Reuse the inherited bounded administrative notification, save/restore host_client, initialize sizebuf and check formatted command length. It is a deliberate admin send, not the per-frame connector. |
| [unknown] End-to-end reconnect, timeout, cancellation, packet ordering and ARM software behavior. | Final software acceptance remains required. No source-only claim proves these outcomes. |

## Main's lean and alternatives

Extend `cl_autoreconnect_t` with timed-request policy only: total deadline,
next attempt time, retry interval, switch-pending flag and explicit timed mode.
Keep its existing phases and socket owner. Existing one-shot starts initialize
the added fields to their inactive values and retain their present timeout and
failure behavior. The new command starts timed mode after copying and validating
operands; a newer explicit request cancels/replaces the older one through the
same cancellation path.

In timed mode, wait_config performs the pending native game switch once at
the existing frame boundary, waits for queued configs and the requested delay,
then starts an attempt. Failure returns to wait_config with the next attempt
time. A total deadline bounds config waits, connection attempts and signon;
successful full signon wins before expiry. Partial sockets/signons are retired
before retry or timeout. Never create another socket or duplicate retry owner.
Verify the requested game still matches before attempts; config commands which
explicitly connect/disconnect/change game retain authority to cancel the policy.
No auto-download is added for this command: a missing game is refused before
disconnect, matching the reference. Existing catalogue workflows remain.

Copy primary safe game/endpoint validators once into the native common owner,
with native-sized bounds and a checked all-mount-root directory query. Reuse
these validators at sender and receiver. Keep public registration small, avoid
new policy headers/modules. Safe formatting requires refusal of whitespace,
quotes, backslashes and semicolons in endpoints, and the reference alphanumeric
single-directory rule for games. Do not silently truncate operands. Reject
nonfinite timing before clamps. Preserve the original numeric control endpoint
for later server-game mismatch recovery after successful async attachment.
Only preserve the explicitly selected legacy wire layout when reconnecting to
the same saved endpoint; a redirected endpoint uses ordinary negotiation.

Alternatives rejected: copy the primary blocking NET_Connect/retry loop or its
second struct (duplicates working connection policy); broad rewrite of native
server-game download/transition (no demonstrated need); new packet protocol or
thread (native stuffed command/frame boundary suffice); per-server/mod allowlist
(not the user's generic design). Review may simplify policy fields or improve
native reuse; preserve the complete reference timed-command behavior.

## Scope, review and acceptance

Write set after stage1 finishes: `Quake/cl_main.c`, `Quake/host_cmd.c`,
`Quake/common.c`, `Quake/common.h`. Estimate <=450 net added production lines;
reopen for another owner, new state machine or material scope growth. Smallest
vertical proof: dedicated sender -> queued client command -> installed game
switch/config drain -> async attempts -> full signon, with cancellation and
timeout. Normal desktop/VR gameplay and existing one-shot/download reconnect
must continue to use their native owners.

Before code: one local requested `gpt-6-astra` / `max` verify-then-critique
advisory, <=1000 words. Verify actual named sources, rank the consequential
decisions, challenge unnecessary state and integration hazards; propose the
smallest accepted adjustment. No edits, nested agents, builds/tests/compiler
or engine probes. Do not re-review rendering, prediction solver, inventory,
quad views or broad networking. Main owns architecture and disposition.
Effective model settings are unexposed, so this is a requested-Astra advisory,
not a certified senior-skill pass.

End-of-implementation software checks: Linux/ARM builds; sender admin-source
restriction and bounded full command; receiver source/length/path/timing
validation; absent installed game without teardown; same-game and cross-game
starts; delay and retry cadence; total timeout during config/connecting/signon;
remote/local success; premature disconnect and partial signon; newer request,
manual connect/disconnect/game cancellation; nontruncated IPv4/IPv6 endpoints;
legacy layout continuity only for same endpoint; original numeric control
endpoint after attachment; ordinary auto-switch and catalogue resume unchanged;
desktop/VR crossplay. The goal remains active until full implementation and
consolidated software qualification, excluding the user's live checks.
