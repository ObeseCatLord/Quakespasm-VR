# NET-017: shared UDP socket and NAT rebinding

Branch `2.0`, 2026-09-27. Astra xhigh senior design review of the existing
vkQuake datagram owner against pinned Quakespasm VR `1327f795`. This is the
implementation decision for NET-017, not a mixed-client runtime result.

The ordinary server loop uses `NET_GetServerMessage` and
`Datagram_GetAnyMessage` (`Quake/sv_user.c`, `Quake/net_main.c`,
`Quake/net_dgrm.c`). Reliable flushes and shutdown also call `NET_GetMessage`
on individual virtual clients (`Quake/net_main.c`, `Quake/host.c`). Those
virtual clients share a listening socket. The current per-client read can
consume another client's packet; the normal server read accepts only an exact
address and port, so it drops a valid packet after NAT port rebinding. The
pinned source has packet/control queues and rebinding logic, but its broader
receive implementation does not replace vkQuake's existing transport owner.

| Review finding | Disposition |
| --- | --- |
| A `GetAnyMessage`-only port leaves individual-client flush reads able to steal packets. | **Adopt:** add one bounded raw-packet inbox at the existing datagram owner, drained by both receive APIs. Keep their normal inline processing and preserve packet order per owner. |
| Source's exact-address selector and per-client selector disagree when a same-IP packet is plausible for multiple sockets. | **Adapt:** one selector for both receive paths. Match the exact endpoint first; otherwise require one sequence-plausible candidate on the same driver, listener, address family and IPv6 scope. Ambiguous packets change neither connection. |
| A reliable DATA duplicate can be the only evidence of a new port after its previous ACK went to a dead mapping. | **Adapt:** ACK the observed source, and rebind only an unambiguous immediate-predecessor duplicate or a valid in-order packet. Never append or deliver a duplicate twice. ACK transmission alone does not prove the sender owns the old session. |
| A previous-port straggler can pull a live socket back to its abandoned mapping. | **Adopt narrowly:** remember the prior endpoint for a finite interval, process attributable stragglers without redirecting the socket backward. |
| Pinned source prunes an established same-IP client after only three seconds of inbound silence if another same-IP client exists, even with no new connection. | **Reject:** two legitimate quiet players can cause an arbitrary player to be disconnected during normal polling. Keep vkQuake's established-client timeout. A new same-IP connection also does not prove which old session it replaces. |
| Copying the source's whole receive/control pipeline would duplicate state and policy. | **Reject:** use bounded deferred packets only where vkQuake's per-client reads need them; retain its control handlers, reliability state and protocol. |

The implementation order is: (1) raw foreign-packet/control deferral on shared
virtual sockets; (2) validated unique-candidate rebinding with prior-port
protection; (3) one focused packet fixture through the actual receive APIs.
The first slice is useful independently: two same-IP clients with different
ports must survive a reliable flush that polls one client while the other's
packet arrives first. Queue entries must be invalidated on socket close,
listener reset and reuse. Exhaustion means packet loss and ordinary reliable
retransmission, never delivery to a different client. A dedicated client
socket retains its existing receive path.

The later fixture must cover two established same-IP clients, rebinding,
lost ACK plus duplicate DATA, ambiguous sequence windows, delayed prior-port
packets, interleaved controls, queue exhaustion, malformed lengths, different
IPv6 scopes, reuse and shutdown flushing. It must assert payload delivery,
ACK destination, outgoing destination and absence of cross-client state
mutation. This is software transport evidence; the user's later live
desktop/VR multiplayer check remains the end-to-end acceptance gate. No
performance measurement is required for this implementation decision.

## Implementation checkpoint

`bbfdbf5e` added the bounded shared-socket inbox, and `54c12de5` added one
selector for exact endpoints, recent prior endpoints and uniquely plausible
same-host port changes. A changed port never wins against an exact endpoint;
ambiguous packets cannot mutate either peer. The prior port is retained for a
short straggler interval, and the inherited 300-second established timeout is
unchanged. `3963e815` bounds control parsing, `c97ccd9f` keeps timeout
iteration on the active list after a drop, `6e0fe3c0` clamps reliable fragment
MSS, and `bdca89fa` retires a peer whose reliable fragment cannot fit.

`tests/datagram_rebind_fixture.c` drives the real per-client receive API with
two same-IP virtual peers and a scripted UDP boundary. It checks deferred
delivery, unique rebinding, outgoing and ACK destinations, delayed old-port
packets, ambiguous windows, malformed packets, queue exhaustion/owner cleanup,
IPv6 scope separation, and the server parser's fatal-fragment disposition.
The Linux debug build and focused UBSan fixture pass. A local dedicated-server
probe was attempted with isolated game data, but this sandbox denied UDP socket
creation; it supplied no multiplayer runtime result. The ordinary server
`Datagram_GetAnyMessage` control interleaving and complete signon remain
unverified by this fixture and need a network-capable software run. Hardware
and performance checks remain with the user after implementation.
