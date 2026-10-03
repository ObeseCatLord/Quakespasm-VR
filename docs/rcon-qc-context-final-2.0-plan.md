# NET-020 RCON command context repair

2026-10-02, production58fb8864. Existing V01/F01 owner; not a new feature.

[Verified: native runtime] Corrected wire probe completes all five public
NetQuake signon commands with actual ACKs and reads actual svc_serverinfo15.
First RCON edict1 while the player is spawned crashes the dedicated process1:
PR_SwitchQCVM: A qcvm was already active. Receipts:
FastGames/qsvr-udp-coexist-requalified-r3t24gfd, profile/result.json/server.log.
The prior probe's extension-handshake-order failure remains separately retained.

[Verified: main addr2line/source] Stack is PR_SwitchQCVM -> native edict command
via Cmd_ExecuteString -> _Datagram_ServerControlPacket -> Datagram_GetAnyMessage
-> NET_GetServerMessage -> SV_RunClients -> Host_ServerFrame. Server network
receive runs while SSQC is active; command handlers expect ordinary console
entry with no active VM. net_dgrm.c currently executes authenticated admitted
RCON immediately without suspending this borrowed VM.

[Verified: read-only QSS-M donor net_dgrm.c2149-2171] Existing handler stores
oldvm, redirects, PR_SwitchQCVM(NULL), executes native command, restores oldvm,
ends redirect, restores net_landriverlevel and Host_EndGame if server inactive
because its enclosing callers retain socket/world state. Reuse this actual
reference rather than adding a queued remote-command service. Main inspected
PR_SwitchQCVM, Host_EndGame and existing map/load/console command owners.

Lean: port these missing donor context/driver/ordinary-abort guards into the
existing authenticated handler, retaining new complete bounded field admission
and normal response capture. Roughly10-15lines, one source file. No command
policy registry, protocol, new VM manager, service, deferred response queue or
discovery owner. Existing Host_Error/Host_EndGame handles nonlocal retirement.

Open senior decisions: verify the borrowed-VM mismatch and donor sufficiency;
normal no-VM and active-SSQC paths, restore only live appropriate context and
landriver; challenge map/load/shutdown and error-unwind interactions if donor
code is insufficient. Prefer narrow existing native owners; unverified behavior
is not evidence for a new layer. Report concrete necessary adaptation, not
a whole NET185 audit. Main then spot-checks and records dispositions before code.

Final rerun: original dedicated public15/ProQuake actual signon, native edict
read before/after movement with interleaved getinfo/status/rules/RCONecho; exact
server movement, truthful advertised dialect and natural quit0. Also actual
RCON world transition/reset or ordinary shutdown boundary selected by review,
and preserve prior ten admission/discovery cases. Update affected shipping
source freshness once implementation settles; earlier58fb cohort cannot qualify
a new production patch. Unavailable GPU/provider/physical execution is outside completion under the
latest user instruction; preserve unverified outcomes and implemented features.

## Verified Astra disposition before code

Local Astra/xhigh Epicurus completed read-only review. Main checked native
NET_ListAddresses global-index mutation, ED_PrintEdict_f's VM entry, ordinary
Host_Error after-CSQC branch and QSS-M Host_Error's redirect cleanup.

| Recommendation | Main disposition |
| --- | --- |
| Suspend/restore borrowed VM around admitted native RCON | Adopt donor boundary. |
| Restore both network driver indices, beyond donor LAN-only restoration | Adopt verified status/address-query mutation. Save incoming indices locally. |
| Flush redirect on ordinary Host_Error nonlocal exit | Adopt QSS-M Con_Redirect(NULL), after CSQC recovery branch before shutdown. |
| Flush/restore drivers before inactive-server abort; restore VM only on continuing path | Adopt existing Host_EndGame ownership; no stale context resurrection. |
| Replace native map/load ownership or introduce queues/generation layers | Reject; static sv.qcvm survives object replacement, receive loop looks up client/edict after polling. |

The verified error-unwind omission expands the write set to net_dgrm.c and
host.c, still roughly15lines. Preserve admission/default/command behavior.
Final tests add native status while connected, successful map/reconnect plus
failed-map and failed-changelevel terminal/output checks. Dedicated native quit0
and native abort1 are distinct expected outcomes. Listen longjmp remains a
separate existing qualification boundary, not proven by dedicated tests.

## Execution progress

Production079f4431 implements the reviewed11-line source repair. Current native
public15 gameplay/status/edict, actual ACK-loss recovery and authored living-v6
restore/movement pass with ordinary quit0; see the
[grouped results](final-grouped-software-current-2.0-results.md). The same engine
passes ten discovery/admission cases. Map/reconnect and failed-map/changelevel
terminal/output checks remain in progress. No unavailable GPU test gates this
repair's software completion.
