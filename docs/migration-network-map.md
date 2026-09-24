# Networking feature map

The migration retains **donor networking**, reuses QSS-M `03a498aa` directly for
generic movement/prediction, and integrates the fork's VR behavior at those
boundaries. The fork's private movement dialect is incompatible with ordinary
QSS-M despite shared FTE names; donor PREDINFO support does **not** implement
player prediction. Older fork prediction symbols below are behavior evidence,
not authority to copy its generic replay implementation.

Evidence pins: **Q**=QSS-M `03a498aa`; **M**=master `1327f795`; **X**=OpenXR `30808413`; **D**=vkQuake `4bc898f2`; **I**=local Ironwail `08d578136ff43d7d1ef38e636dfbfd3e844be7cd`. These identify the supplied full commits, except I, pinned locally. File stems below mean `Quake/<stem>.c`; `protocol` and `quakedef` mean `.h`. **F** references X’s implementation shared with M unless distinguished.

Disposition: **R** reuse fork; **A** adapt at existing owner; **D** donor equivalent candidate; **G** missing goal. Acceptance cases are proposed, **not executed**.

Packet, transport, command, and prediction behavior:

| ID | Behavior and reference evidence | Donor evidence | Disposition → destination owner | Acceptance case |
|---|---|---|---|---|
| NET-001 | NQ15/Fitz666/RMQ999 selection, extension intersection, per-peer limits: Q `sv_main:SV_Protocol_f/SV_SendServerinfo`. F deliberately fixes RMQ and bypasses negotiation. | `sv_main:1045,1437` retains selectable protocols and `cmd pext`. | **A** → `protocol/sv_main/cl_parse` | On one server, a desktop vkQuake client and an OpenXR VR client connect simultaneously, move, aim, fire, observe each other, change maps, and reconnect. Each peer keeps its negotiated public/private framing and optional VR fields. Desktop startup and play require no XR runtime. |
| NET-002 | Private mandatory extensions: F `protocol:53–72`, `cl_parse:701`, `sv_main:1439`. **0x80 means EXPLICITCMDMSEC in F, INFOBLOBS in Q**; private svc57 also collides with DP entities. | `protocol:50–57` advertises replacement/PREDINFO only. | **A** → protocol negotiation/dispatch | A public QSS peer cannot accidentally enter private move/ACK decoding. |
| NET-003 | Replacement deltas: baseline resets, removals, extended entity numbers, changed fields, visibility/customization and resend history. Q/F `SVFTE_CalcEntityDeltas`, `CLFTE_ReadDelta`. | Same named implementations in `sv_main/cl_parse`. | **D** → `sv_main/cl_parse` | Entity disappearance, reappearance and lost reset recover correctly. |
| NET-004 | Split snapshots with transport-sequence ACKs and stat/entity retransmission. F additionally repeats a self-contained owner and movement state, preserves contiguous ACKs and bounds packet progress: `sv_main:2795,4191`; `cl_parse:1610`. | `SVFTE_WriteEntitiesToClient`, `SVFTE_Ack` provide baseline machinery, not F’s owner guarantee. | **A** → snapshot writer/parser | Losing one split packet does not orphan player state or stall pending entities. |
| NET-005 | RMQ coordinate/angle precision and richer solid encodings for prediction. Q `cl_parse:459`; F `cl_parse:1376`, `MSGFTE_WriteEntityUpdate`. | RMQ packing exists; `CLFTE_ReadDelta` lacks NEWSIZEENCODING branching. | **A** → `protocol/common/sv_main/cl_parse` | Float coordinates and BSP/hull/custom bounding boxes decode symmetrically. |
| NET-006 | Byte/int/float/string stats, PREDINFO clientdata replacement, velocity/ground state and movement-variable stats. Q `SV_CalcStats`, `pr_ext:PMSV_SetMoveStats`; F `sv_main:2670`, `pmove:2580+`. | `SVFTE_WriteStats` exists; movement-stat producer/consumer absent. | **A** → `sv_main/cl_parse/pmove` | Changed gravity/speed reaches prediction; lost stats resend. |
| NET-007 | Staged signon, dynamic precaches, extended baselines/statics and loading keepalives. Q/F `SV_SendPrespawn*`; F `SV_SendServerinfo` stages sounds. | `SV_SendPrespawn*`, `CL_ParsePrecache`, `CL_KeepaliveMessage`. | **A** → `sv_main/cl_parse` | Large precache lists complete signon without packet overflow. |
| NET-008 | Concrete parser gaps: F lacks `svcfte_setangledelta` and `svcfte_cgamepacket` dispatch; its `UF_DRAWFLAGS` omits conditional absolute-light byte. Q `cl_parse:524,4442,4807`. | Correct byte consumption/dispatch at `cl_parse:370,1912,2191`. | **G/D** → `cl_parse`, existing QC event interface | Each message followed by another opcode remains aligned; angle correction respects VR handling. |
| NET-009 | CSQC entity create/update/remove, `SendEntity/SendFlags`, ACK resend and `enablecsqc` lifecycle. Q `sv_main:1093`; F `sv_main:2957`, `cl_parse:961`, `host_cmd:2386`. | PEXT1_CSQC declaration and event support do not supply this entity transport. | **R** → `sv_main/cl_parse`, QC hooks | Lost custom-entity update/removal recovers; enabling CSQC resends state. |
| NET-010 | Sequenced commands, wrap handling, duplicate rejection, first-two suppression. F adds current-plus-two-prior commands and 24-record server queue: `cl_input:947`, `sv_user:1283,1461`. Q sends one command. | `CL_SendMove/SV_ReadClientMove` supply single-command sequence handling. | **R** → `cl_input/sv_user` | Loss preserves brief fire/impulse; duplicates apply once across wrap. |
| NET-011 | Explicit 1–125 ms duration, monotonic sampling/fractional carry; timestamp no longer donates lost time. F `cl_input:CL_SampleMoveMsec`, `sv_user:1177`. Q derives duration from clamped timestamps. | `SV_ReadClientMove` uses timestamp for ping, not command simulation. | **R** → `cl_input/sv_user` | Hitch, packet gap and clock correction cannot create movement catch-up. |
| NET-012 | Render/input/network cadence separation. Q/F support negative `host_maxfps` pacing, accumulated input and one network pass; F retires `cl_netfps`. | `host:1081` uses a catch-up loop; `host_phys_max_ticrate` and dedicated default differ. | **A** → `host/cl_main` | Multiple render rates preserve intended command duration and dedicated/listen cadence. |
| NET-013 | Real prediction: authoritative origin/velocity/solids, movement variables, command-history and pending-command replay. Q `cl_main:1225`; F `CL_SetupPlayerPrediction/CL_ReplayPlayerCommands`. | Scoped client/server code has no `PM_PlayerMove` or equivalent replay. | **R** → `cl_main/pmove` | Remote walking, stairs, water and collisions reconcile against server movement. |
| NET-014 | ACK authority, prediction permission, mode/discontinuity epochs, error smoothing/quarantine. F `SV_WriteMoveAckPayloadToMessage`, `CL_ParseMoveAckPayload`, `CL_CheckPredictionError`. | Only sequence/PREDINFO ACK machinery. | **R** → `sv_main/cl_parse/cl_main` | Teleport, mode switch and repeated unexplained errors reset/restrict prediction coherently. |
| NET-015 | Extended buttons, weapon/cursor inputs, QC input globals and typed CSEv requests. Q `sv_user:509,705`; F `CL_WriteUsercmd`, `SV_ReadQCRequest`, `pr_cmds:PR_GetSetInputs`. F uses private extbits, not Q’s long-button layout. | Basic buttons; cursor flag declaration does not implement Q input transport. | **A** → `cl_input/sv_user`, QC registry | Cursor/weapon and typed events reach correct QC arguments without disturbing following commands. |
| NET-016 | NQ reliable fragmentation, ACK/EOM, retransmission, unreliable ordering and MSS changes between messages. Q/F/D `Datagram_SendMessage/ReSendMessage`. | Existing transport is reusable; F adds explicit length validation and MSS clamping. | **A** → `net_dgrm/net_main` | Large reliable signon survives retransmits; malformed lengths are rejected. |
| NET-017 | Shared listening sockets and NAT handling. Q callback receive; F adds queued demultiplexing, validated port remap, old-address suppression, stale-slot pruning and explicit timeout checks. | `Datagram_GetAnyMessage` supports shared sockets, fewer fork safeguards. | **A** → `net_dgrm/net_main/sv_user` | Two clients behind one NAT remain distinct through remap/reconnect. |
| NET-018 | Loopback sequence identity and reliable/unreliable buffering/reset. F `net_loop:Loop_GetMessage/Loop_SendUnreliableMessage`. | Sequenced loopback plus larger `NET_LOOPBACKBUFFERS` storage. | **A** → `net_loop` | Local signon/splits ACK correctly; reconnect clears sequence state. |
| NET-019 | IPv4/IPv6 drivers, nonblocking sockets and address handling: Q `net_udp/net_wins`, driver tables. F retains older IPv4 interfaces. | `UDP4_*`, `UDP6_*`, Windows IPv6 implementations. | **D** → `net_bsd/net_win/net_udp/net_wins` | IPv4 and IPv6 direct connections work with platform-specific drivers. |
| NET-020 | LAN queries, public-master discovery/heartbeats, getinfo/status, RCON, challenge-connect and ProQuake angle/NAT handshake. Q `net_dgrm`; F chiefly classic control/connect. | `_Datagram_ServerControlPacket`, `_Datagram_SearchForHosts`, `_Datagram_Connect`. | **D/A** → `net_dgrm/net_main` | Discovery/control requests coexist with gameplay; advertised dialects match actual parsers. |
| NET-021 | Serverinfo/userinfo key propagation, name/colors and QC-visible metadata. Q `cl_main:6994+`; F lacks matching handlers in scoped client/server files. | `SV_UpdateInfo`, `CL_ServerExtension_FullServerinfo_f`, userinfo handlers. | **D** → `cl_main/sv_main`, QC info interface | Initial and incremental metadata update after reconnect/map change. |
| NET-022 | Server-directed game switch, catalogue installation and reconnect. F `CL_ServerModDownload_*`, `CL_MaybeSwitchServerGame`. Q’s per-file download pipeline differs. | Server gamedir information exists; no equivalent managed install/resume flow here. | **R** → `cl_main/cl_parse`, existing catalogue owner | Missing mod installation resumes the intended connection; cancellation cleans state. |
| NET-023 | VR aim/muzzle and room-scale samples, body-relative capability and finite/outlier validation. F `CL_WriteUsercmd`, `SV_ReadUsercmd`, `SV_ValidateVRRoomScaleUsercmd`. | No private payload. | **R** → `cl_input/sv_user`; movement application remains physics-owned | Rotation/teleport preserves relative aim; invalid tracking contributes no movement. |
| NET-024 | Akimbo/Berserk poses and weapon-contact payload/profile negotiation. F `MOVEEXT_VR_AKIMBO/CONTACT`, `SV_WriteWeaponContactProtocol`. | Absent. | **R** → `cl_input/sv_user/sv_main`; melee policy stays existing owner | Hand ordering, negotiated profile and queued command identity remain consistent. |
| NET-025 | Raw/trusted Gorilla input, generations, contact identity and separately committed state sequence. F `CL_WriteUsercmd`, `SV_GorillaTrustedCommand`, ACK writer. | Absent. | **R** → existing command/PMove/Gorilla owners | Surface reuse, release and authority change invalidate stale constraints. |
| NET-026 | VRIK v2/v3 framed poses, opt-in, sequence/generation, stale/inactive handling; avatar identity/selection messages. F `protocol:75+`, `CL_AppendVRIKPose`, `SV_ReadVRIKPose`, capability handlers. | Absent. | **R** → protocol/codec and client/server transport; avatar owner retains identity | Unsupported versions fall back; malformed body preserves framing; slot reuse rejects stale poses. |
| NET-027 | Private voice framing/capability, source generation, rate limits and gameplay-first packet budgeting. F `svc_voice/clc_voice`, `SV_Voice*`, `CL_ParseVoicePacket`. | FTE voice acceptance is not F’s private transport. | **R** → existing voice transport callsites | Voice saturation cannot displace movement; stale source generations drop. |

Gameplay policy remains separate:

| ID | Behavior and reference evidence | Donor evidence | Disposition → destination owner | Acceptance case |
|---|---|---|---|---|
| NET-028 | PMove/QC authority: engine-compatible versus command-QC lifecycle, local-SP exclusion, ladder/customphysics gates, legacy tap latching. F `sv_user:2254`, `sv_pmove_policy.h`, `sv_phys:SV_RunPMoveForEntity`. Q’s simpler PreThink velocity restoration is superseded. | `SV_Physics_Client` retains frame-based QC; `SV_RunClientCommand` declaration alone is insufficient. | **R/A** → `sv_user/sv_phys/pmove` | Compatible mods predict; native-QC movement and one-frame actions retain their lifecycle. |
| NET-029 | Coop collision/pickup/respawn policies remain physics/QC-owned. **M `sv_phys:6227` consumes tracking during MOVETYPE_NONE; X predates that guard**, plus teleport cleanup hooks in `pr_edict/sv_phys`. | No equivalent fork policy or tracking guard. | **R from M**, reconcile X → existing gameplay owners | Frozen remote player never moves or accumulates tracking debt; death/entity reuse clears lifecycle state. |

The following are **proposed optional scope**, not adopted requirements. Absence searches covered M, X and D’s `protocol`, `net*`, `cl*`, `sv*`, `host*`, `pr*`, `pmove*` and client definitions; relevant implementations/registrations were inspected beyond name matching.

| ID | Existing source behavior | Evidence both fork and donor lack it | Proposed owner; acceptance |
|---|---|---|---|
| CAND-NET-001 | DP7 client entities/input and BJP3 dialect: Q `CLDP_ParseEntitiesUpdate`, `cl_input:666`, `SV_Protocol_f`. | No DP7/BJP3 constants or decoder branches; D’s connect string nevertheless lists them. | Protocol/client-server parsers; connect/decode only genuinely implemented dialects. |
| CAND-NET-002 | In-band legacy and chunked downloads, retry window, range/completion validation: Q `CL_Download_Data/Chunked`, `DLC_RequestDownloadChunks`, `Host_AppendDownloadData`. | F has declaration/`nextdl` routing only; D registers ignored download commands. Catalogue installation is different. | `cl_main/host_cmd/sv_main`; interrupted/reordered download completes or fails cleanly. |
| CAND-NET-003 | Cancellable frame-driven connection retries: Q `CL_BeginConnect`, `NET_DatagramConnectFrame`. DNS remains synchronous. | F reconnect scheduling still calls blocking `NET_Connect`; D similarly blocks. | `net_dgrm/cl_main/host`; cancel an unreachable connection while UI remains responsive. |
| CAND-NET-004 | ICE/WebRTC driver and signaling: Q `net_bsd:70`, `net_dgrm:ice_offer/ice_ccand`. | Neither driver table nor scoped networking has ICE hooks. | Existing net-driver boundary; browser/native session traverses intended NAT. Build/runtime availability remains unverified. |
| CAND-NET-005 | Full CSQC prediction access: Q `pr_ext:8397` getinputstate, client `PF_cs_pmove`, builtin347. | F exposes input hook/servercommandframe but builtin347 is SSQC-only and lacks getinputstate; D lacks these APIs. | QC registry/`progs/pmove`; QC retrieves valid history/pending input and predicts it. |
| CAND-NET-006 | Up to 255 player slots: Q `quakedef:279`, server/client slot handling. | M/X/D cap `MAX_SCOREBOARD` at16; F’s MAXPLAYERS flag does not remove that bound. | Client/server capacities and protocol validation; slot17+, updates and disconnects work. |

Important omissions: Q’s synthetic `pq_lag` sender, ProQuake team/ping metadata, map-CRC reporting, numeric/hash join-password extension and detailed loss reports are not implied by donor control compatibility. Q’s INFOBLOBS/BSP acceptance flags and commented VRINPUTS likewise do not establish implemented parity. Q FTE voice is superseded by the fork transport; audio internals were excluded.

Ironwail’s networking candidate `sv_netsort` is already present in both fork and donor, so it does not qualify as new optional scope.

This is a static behavior map, not exhaustive runtime parity. Bounded follow-ups are dialect/dispatch closure, newer M gameplay fixes missing from X, optional-candidate dependency validation, and eventual acceptance cases above.

For the `2.0` mixed-client gate, `SV_SendServerinfo` selects the private QSVR
profile per client only after its explicit offer and matching server settings;
`SV_ReadClientMessage` dispatches private and ordinary movement per client.
Those branches are architectural evidence, not a cross-play result. The gate
requires one live session with desktop and VR peers together, including a
public peer beside a private VR peer where the server permits both, then two
private peers with only one sending VR samples. Check that desktop input and
prediction remain ordinary, that the VR peer's hand/room-scale data reaches
the same authoritative world, and that both see consistent damage, pickups,
death/rejoin and map transitions. Neither a desktop-only private loopback nor
a separate single-headset run proves this mixed case.

The current private command carries the VR hand and muzzle pose to the server,
where `SV_BeginPrivateVRWeaponPose` temporarily applies it while the player's
weapon think runs. That is enough source evidence for an intended shared
damage/projectile world; it does not distribute a remote tracked avatar to
other clients. NET-026's VRIK pose transport and avatar rendering remain a
separate parity gate. A desktop peer must still see and interact with the VR
player correctly when remote hand tracking is absent or unavailable.

### Mixed-peer senior review disposition

A local Astra Max review verified the per-client negotiation and private-only
VR payload boundaries, then returned its highest-priority findings before the
full network critique was complete. Treat this as a focused disposition, not
final cross-play sign-off.

| Review finding | Disposition |
| --- | --- |
| Separate desktop and VR connections do not prove simultaneous cross-play. | **Adopt.** Qualify one dedicated server with an ordinary public vkQuake desktop peer and private OpenXR VR peer, then repeat with two `2.0` peers and only one in VR. Observe movement, aim/damage, pickups, death/rejoin and map transition from both clients and the server. |
| `sv_qsvr_private` defaults off and public movement omits VR hand/room-scale data. | **Adopt.** Keep the development gate until VR and mixed-peer gameplay qualify; report public connectivity separately from full VR behavior. |
| Desktop `2.0` also offers QSVR, so a desktop-plus-VR run may exercise two private clients. | **Adopt.** Include both public/private and private/private pairings; do not infer public compatibility from the latter. |
| Add a desktop-versus-VR network dialect or duplicate movement owner now. | **Reject.** Current negotiation and movement dispatch are per client; no verified incompatibility requires another layer. Reopen only for a demonstrated mixed-peer failure. |

### Server-mod reconnect senior review disposition

The local Astra xhigh review checked the donor and inherited
parser/connection code before critiquing the adapter.
This is a design decision for NET-022, not implementation or runtime sign-off.

| Review finding | Disposition |
| --- | --- |
| A disconnect in `svc_serverinfo` would leave the parser reading the packet tail. | **Adopt.** Validate the complete bounded gamedir string, then return out of the message dispatcher immediately when redirecting. |
| Reconnect needs one owner and must preserve the explicit legacy `qsvr1` choice across internal disconnects. | **Adopt.** Keep one client operation with an identity/cancellation token, the original endpoint and explicit legacy selection; renegotiate capabilities on each attempt. Extract a nonfatal connection helper under the current command wrapper and bound retries and signon. |
| Game switching queues configuration commands. | **Adopt.** Reconnect only after the switch and queued commands settle; a fixed delay alone is insufficient. |
| Donor `NET_Connect` can block for three 2.5-second datagram waits on failure. | **Adapt after code inspection.** Do not schedule repeated synchronous connection attempts from the render frame. The first installed-mod slice may make one post-switch attempt and report failure; full failed-server retry parity needs a later nonblocking adaptation of the existing transport, not a second socket owner. |
| Inherited case-insensitive game identity differs from donor exact matching; pak0 presence differs from a valid loose mod. | **Adapt locally.** Resolve actual installed directory spelling for the redirect and compare semantic identity in that operation. Keep donor `COM_GameDirMatches` unchanged and do not use the catalogue pak0 predicate for all installed mods. |
| Inherited missing-mod flow refreshes automatically and all current catalogue entries are unverified. | **Adapt.** Keep ordinary catalogue browsing refresh explicit, but allow the inherited automatic catalogue lookup after the user explicitly connects to a server requiring a missing mod. Never install without consent for the exact gamedir and unverified package; re-resolve it on acceptance and cancel only an operation-owned transfer. |
| Public PEXT2 peer auto-switch policy is a human preference. | **Resolve conservatively.** Retain the donor's warning-only public behavior by default; make public auto-switch an explicit opt-in. Private QSVR switching follows the inherited behavior when enabled. |

The smallest later software proof covers installed/missing gamedirs, malformed
and oversized strings, cancellation/superseding connects, failed connect,
stalled signon, repeated mismatch and packet-tail abandonment. The user's live
mixed-peer testing remains separate from implementation work.
