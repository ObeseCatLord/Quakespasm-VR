# CAND-NET-004: QSS-M ICE/WebRTC transport adapter

Status: implementation plan; tests are final batch only.
Reference QSS-M 03a498aabc411e2e739adc815c5536b161b9626e, Quake/ice.

Reuse the complete FTE-derived vendor implementation and its licenses, including
ICE/STUN/TURN, mDNS, SCTP, DTLS and FTE WebSocket broker protocol. Add its
NQICE functions to vkQuake's existing net_driver_t table beside Datagram/Loopback.
Do not create a new signaling service, gameplay protocol, client prediction owner,
or libdatachannel-based alternate implementation. Native UDP remains unchanged.

Both broker room addresses (`connect /room`) and explicit donor broker URLs use
NQICE_Connect, which returns a qsocket with transport establishment pending.
Normal host frames continue through NQICE_GetMessage/CanSendMessage: existing
client signon/retry owner supplies cancellation/timeouts/disconnect, not a second
client state machine. Cache the original ICE address in target qsocket.connectaddress
for mod reconnect; never substitute a masked peer display name as a connect endpoint.
Ordinary numeric/hostname UDP still uses existing Start/Frame/Cancel machinery.

The borrowed UDP game socket integration follows donor Share/Unshare lifetimes
and packet classification: only unmatched STUN/DTLS goes to ICE, never established
Quake datagrams. Clear borrowed references before closing/rebinding a game socket.
Retain bounded donor broker authentication rules for unsolicited signaling and
DTLS fingerprint checks. No unauthenticated network cvar mutation.

Registration is explicit (`sv_port_rtc` default empty) rather than automatically
advertising every native server to the public broker. The configurable net_ice_broker
and net_ice_servers retain donor STUN/TURN/broker semantics. Documents explain
room/URL connection, TLS provider, candidate privacy and optional relay.

Require a real TLS provider for WebRTC browser compatibility. Prefer donor GnuTLS
path when available; feature builds without TLS must clearly identify ICE-only
capability or disable unsupported WebRTC schemes rather than claim encrypted
browser compatibility. Wire feature options/dependency detection into Linux Make
and Meson, Windows final build configuration once all source work is complete.
Use genuine platform cryptographic random; do not adopt donor weak RNG fallback
for credentials. Private cert/key material uses OS writable per-user storage or
in-memory ephemeral credentials, not installation folder or runtime release archive.

Worker write scope: new Quake/ice vendor directory and compatibility adapters,
new docs/ice-transport.md and focused fixtures. Main owns net_bsd/net_main/net_dgrm,
headers and Make/Meson integration; worker reports exact hooks and compatibility
requirements. Preserve target qsocket size/fields and socket API boundaries.

Final verification: compile provider-enabled and disabled configurations; local
peer reliable/unreliable payloads, broker failure/cancellation, listen/rebind/close
and mod reconnect. Native UDP co-op unaffected. Genuine remote NAT/browser/
headset tests are user followup, not substitute claims from counters or mocks.

Official specifications: [ICE RFC8445](https://datatracker.ietf.org/doc/html/rfc8445),
[WebRTC data channels RFC8831](https://datatracker.ietf.org/doc/html/rfc8831),
[DCEP RFC8832](https://datatracker.ietf.org/doc/html/rfc8832). The broker protocol
itself is defined by the pinned FTE-derived QSS-M source, not a new standard.
