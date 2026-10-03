# NET-020 discovery/control startup binding repair

2026-10-02. Existing V01/F01, not an additional feature or checklist owner.
Final grouped software qualification identifies an actual initialization gap.

[Verified: current source] Quake/net_dgrm.c defines sv_public with NULL initial
string, sv_reportheartbeats, com_protocolname, seven net_masters and rcon_password.
Public OOB query/heartbeat and RCON handlers already consume these exact objects.
No current registration/reference writes initialize their runtime cvar values.
Host_Init calls NET_Init then SV_Init before startup configuration executes.

[Verified: actual current packaged native UDP] Classic serverinfo works. The
private autoexec sets the hostname successfully, but configured RCON still returns
"rcon is not enabled on this server". Native master heartbeat is absent. First
RCON test received delayed duplicate classic replies; filtering actual control
opcode corrects that observer, and then establishes the disabled reply plus normal
server exit. Receipts: FastGames/qsvr-native-discovery-filtered-fe3dskwy and earlier
native-discovery-final-vtdcj8a5/diagnostic-5whq5xb6/packet-1akaevmh failures.

[Verified: read-only donor] QSS-M Quake/sv_main.c:1755–1766 initializes/registers
these objects in SV_Init, chooses public1 only for dedicated, registers every
named master and rcon_password. Native vkQuake has the dormant handlers but lacks
those registrations. This is the missing narrow startup adapter; existing
buffers, UDP drivers, control dispatch, reliable transport and cvar owners remain.

Proposed repair: register existing objects in SV_Init using the donor structure.
Initialize sv_public to0, preserving current native desktop/dedicated unpublished
behavior unless explicitly configured. Donor dedicated-default1 is considered;
no requested default requires automatic public advertising. Register every named
master, report/protocol-name and empty-default RCON password. Expected one source
file, roughly20added lines. Do not create a control service, new protocol, secret
manager, heartbeat clock, queue, client rcon command or server challenge system.

Senior decision: verify actual gap and target scope, challenge default choice and
whether registration at existing SV_Init is sufficient; identify any load-bearing
adjacent admission/framing defect that enabling these native paths exposes. This
is a bounded repair review, not a new185-feature audit or whole-goal signoff.

After coherent implementation, use real private dedicated server/UDP requests and
controlled loopback master only: classic/OOB query and cookie echo, native rule
enumeration, disabled/wrong/valid RCON controls and actual command-store change,
public0 suppression/public1 explicit heartbeat, normal shutdown. Every default
external master is cleared in the private config before enabling publication.
Exact responses/ownership count; no hand-assigned cvar/pipeline/native results.
Retain failed probes, refresh affected native graph and shipping inputs only.
GPU/physical/runtime-provider qualification stays under its existing owner.

## Astra review disposition before implementation

Local Astra/xhigh Goodall completed read-only source verification. Main inspected
its cited native handlers and MSG_ReadStringBuffer implementation.

| Finding | Disposition | Narrow implementation boundary |
| --- | --- | --- |
| Missing existing cvar registrations | Accept | Reuse donor SV_Init registration, public0 in both modes, empty RCON password. |
| Authenticated EOF/oversized RCON command can execute a prefix | Accept | Validate both complete bounded fields within existing control packet before redirection or command execution; no global reader rewrite. |
| Raw challenge may inject fields or truncate correlation | Accept | Admit an optional single bounded challenge, allow ordinary terminal newline, reject delimiter/control/overflow and preserve normal echo. |
| NET-020 includes connected gameplay and other existing owners | Accept | Add interleaved native gameplay checks; this slice cannot waive remaining challenge/ProQuake/master obligations. |
| Default public1 on dedicated | Decline | Preserve current unpublished default; explicit public1 enables existing paths. |

Estimate revised from one file/~20 lines to two files with local admission checks.
No new state machine, protocol, service, clock or ownership layer. Qualification
adds malformed and oversized RCON password/command, unsafe/overlong OOB challenge,
password omission from rule enumeration and actual connected gameplay coexistence.
Affected source and shipping freshness must be refreshed after the repair.
