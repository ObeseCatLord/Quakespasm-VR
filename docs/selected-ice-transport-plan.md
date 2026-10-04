# CAND-NET-004: QSS-M ICE/WebRTC transport adapter

Status: implementation integrated; strict native GnuTLS build, local payload/lifecycle fixture and direct UDP full-game signon passed. Platform builds are the final batch.
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

Require a real TLS provider for WebRTC browser compatibility. Make and Meson
prefer the donor GnuTLS path when available and fall back to OpenSSL. With no
provider, automatic ICE selection disables the driver; an explicitly enabled
ICE build errors rather than claiming browser capability. Windows remains an
optional explicit-provider configuration, with no provider assumed.
Use genuine platform cryptographic random; do not adopt donor weak RNG fallback
for credentials. Private cert/key material uses OS writable per-user storage or
in-memory ephemeral credentials, not installation folder or runtime release archive.

Worker write scope: new Quake/ice vendor directory and compatibility adapters,
new docs/ice-transport.md and focused fixtures. Main owns net_bsd/net_main/net_dgrm,
headers and Make/Meson integration; worker reports exact hooks and compatibility
requirements. Preserve target qsocket size/fields and socket API boundaries.

Adapter result: the vendor tree is copied from the pinned revision with its
source notices and root license. `ice_quake.c` supplies a side-effect-free
`NQICE_IsAddress`, caches the original ICE connect text in `connectaddress`,
uses a singular-message server queue for this target's callback shape, and
exports `NQICE_ProcessSharedPacket` for unmatched STUN/DTLS forwarding. It
defaults `sv_port_rtc` to empty, uses OS cryptographic random without a weak
fallback, and keeps generated DTLS credentials in memory rather than writing
to `com_basedir`. The detailed handoff, provider contract, and deferred test
matrix are in `docs/ice-transport.md`.

The staged main integration appends bounded `*wsaddr`/`*fp` server-info fields
using `NQICE_GetWsAddr()` and `NQICE_GetFingerprint()` when nonempty. One donor
ICE/DTLS session owner accepts the shared port; it passes `ice_offer` and
`ice_ccand` to native Datagram control parsing only while its provider has
successfully decrypted that callback. Native private co-op remains unchanged.

Final verification: compile provider-enabled and disabled configurations; local
peer reliable/unreliable payloads, direct DTLS transport acceptance and encrypted same-port
broker-control dispatch, queue-overflow retirement, listen/rebind/close and mod
reconnect. Native UDP co-op unaffected. Genuine remote NAT/browser/headset tests
are user followup, not substitute claims from counters or mocks.

Official specifications: [ICE RFC8445](https://datatracker.ietf.org/doc/html/rfc8445),
[WebRTC data channels RFC8831](https://datatracker.ietf.org/doc/html/rfc8831),
[DCEP RFC8832](https://datatracker.ietf.org/doc/html/rfc8832). The broker protocol
itself is defined by the pinned FTE-derived QSS-M source, not a new standard.


Final native results: UDP and DTLS reliable/unreliable payloads, encrypted broker
parser forwarding, reconnect/relisten and bounded client/server FIFO failure
pass. A separate two-process dedicated-server/client probe passes complete
private signon, prediction, movement and firing over `udp://`. It caught and
fixed direct clients erroneously polling a room broker. DTLS fixture coverage
still stops at transport acceptance/payload; external room/NAT/browser tests
are not claimed. The driver is optional on Windows until a TLS SDK is supplied.
