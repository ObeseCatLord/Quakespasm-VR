# ICE/WebRTC transport adapter

`Quake/ice` is a complete vendor import from QSS-M commit
`03a498aabc411e2e739adc815c5536b161b9626e` (the pinned donor tree), with
target-only compatibility changes in `ice_quake.c`/`ice_quake.h` and a narrow
shared-socket reuse hook in `ice_main.c`/`ice_private.h`.
`Quake/ice/LICENSE-QSS-M.txt` is the donor's root `LICENSE.txt`, copied without
modification. Source-file notices remain intact.

The adapter adds an ICE net driver beside Loopback and Datagram. It does not
replace native UDP: normal numeric and hostname `connect` requests continue to
use Datagram's existing asynchronous connection machinery. ICE recognizes
`/room`, `rtc[s]://`, `ice[s]://`, `tcp://`, `tls://`, `http[s]://`,
`udp://`, `dtls://`, and `ws[s]://` forms. `NQICE_Connect` copies the original
input into `qsocket_t.connectaddress`; masked peer text is display-only and is
never used for a reconnect.

For client connection authentication, ICE first reads the target's native
`cls.userinfo` `password` field (for example, `setinfo password value`). A
private adapter-owned legacy `password` cvar remains only for donor-compatible
ProQuake numeric hashing and server admission when no userinfo field exists.
It does not alter native Datagram password or userinfo behavior.

Servers do not register unless `sv_port_rtc` is set. Its default is empty.
Set `sv_port_rtc /room` to use a chosen room, or `/` to ask the configured
broker for a generated room. `net_ice_broker` and `net_ice_servers` keep the
donor broker/STUN/TURN syntax. `net_ice_allowmdns`, private-candidate, STUN,
TURN, and relay-only controls preserve the donor candidate-privacy policy.

New DTLS ClientHello packets have no wire-level marker that distinguishes a
direct `dtls://` client from a broker `/udp` client on the same game port. The
adapter therefore uses one donor ICE module and its existing direct DTLS accept
path for both. Only after the TLS provider has decrypted a packet does the ICE
game-packet callback recognize `ice_offer` or `ice_ccand` and call the native
Datagram broker parser. Its authenticated-send context exists only for that
callback, so plain UDP controls and a second competing DTLS listener cannot
claim the port. Empty `sv_port_rtc` disables registration, not local direct
connections.

## Required core integration

These changes are owned by the main integration work, not this vendor
directory. All five interfaces below are integrated; native validation is recorded below.

1. Add `void *driverdata2` after `driverdata` in `qsocket_t` and initialize it
   to `NULL` in `NET_NewQSocket`. ICE stores its per-peer `icestate_s` there;
   `driverdata` remains the broker/module state.
2. In `net_bsd.c` and `net_win.c`, include `ice/ice_quake.h` and add the ICE
   driver after Datagram. Its exact callback slot is `NQICE_GetAnyMessage`, not
   the donor callback-shaped `NQICE_GetAnyMessages`.
3. Add `NET_ConnectSpecial` before Datagram's async path. If
   `NQICE_IsAddress(host)` is true, set network time and the ICE driver level,
   then call only `NQICE_Connect(host)`. Do not send these addresses through
   Datagram. All other inputs retain the present Datagram start/frame/cancel
   owner.
4. In `net_dgrm.c`, call `NQICE_UnshareGameSockets()` before closing or
   rebinding listening UDP sockets, then call `NQICE_ShareGameSocket()` for
   each successfully bound IPv4/IPv6 game socket. After ordinary Quake socket
   matching fails, forward only packets whose first byte is STUN (`0..3`) or
   DTLS (`20..63`) to `NQICE_ProcessSharedPacket(data, len, &addr)`. Never
   forward an established Quake datagram. The wrapper queues any completed
   message; the next ICE driver `NQICE_GetAnyMessage` returns one queued
   socket/message pair, so no callback packet is overwritten or dropped.
   `NQICE_UnshareGameSockets` also releases every host-module send-only wrapper
   before UDP closes a descriptor, preventing an old wrapper from targeting a
   newly reused descriptor number.
5. Keep native `_Datagram_BrokerPacket` as the control-format authority. The
   ICE callback invokes it only for a successfully decrypted direct-module
   packet whose command is `ice_offer` or `ice_ccand`; its temporary
   `BrokerDTLS_IsAuthenticated()` context permits replies through that same
   ICE peer. Advertise `NQICE_GetWsAddr()` and
   `NQICE_GetFingerprint()` only when available. Unauthenticated network data
   must not set any ICE cvar. The target routes broker control only while the
   DTLS session is authenticated; direct unauthenticated UDP offers are not
   accepted. Main appends donor's guarded `\\*wsaddr\\` and `\\*fp\\` fields
   from `NQICE_GetWsAddr()` and `NQICE_GetFingerprint()` in
   `Datagram_GenerateGetInfoString` for browser bootstrap. The local fixture
   exercises the encrypted same-port control boundary; a real broker `/udp/...`
   room exchange remains deferred.

The singular target callback differs from the donor's multi-message callback.
The adapter uses a 64-message FIFO (up to `NET_MAXMESSAGE` each) and retires a
peer on overflow, rather than silently acknowledging a reliable message that
cannot be delivered. Reliable receive capacity is checked before ACK/sequence
advance; decoder failure similarly retires the server peer or fails the owning
client. Retirement is deferred until `ProcessModule` or shared
`ProcessPacket` returns: queued references are discarded and the adapter uses
`SV_DropClient` for an owning server slot, or `NET_Close` for an unowned
qsocket. It never pre-marks `qsocket.disconnected`, because native `NET_Close`
intentionally ignores such sockets. The singleton client queue is cleared on
owner close/failure and rejects a different owner before dequeue.

## Build provider contract

Every enabled vendor source must compile as C with `-DUSE_ICE`:

`ice_gnutls.c ice_main.c ice_mdns.c ice_openssl.c ice_quake.c ice_sctp.c ice_socket.c ice_stream.c json.c md5.c sha1.c sha2.c`.

Make and Meson prefer GnuTLS when it is available, compiling with
`-DUSE_GNUTLS -DGNUTLS_STATIC` and its `pkg-config gnutls` flags and libraries.
If GnuTLS is unavailable, both support OpenSSL with `-DUSE_OPENSSL` and its
dependency. `USE_ICE=auto` (Make) and `use_ice=auto` (Meson) disable the
driver when neither provider is found; an explicit enabled request fails
configuration instead. This prevents a no-provider binary from claiming
WebRTC/browser capability. Windows enables ICE only when either provider and
its headers/import libraries are deliberately supplied; neither provider is
assumed there.

The adapter rejects TLS/DTLS URLs without a TLS provider. It never falls back
to `rand()`: failed operating-system crypto randomness disables ICE traffic.
Server private credentials are supplied by explicit command-line paths,
system certificate paths, or one ephemeral in-memory identity. They are never
written to `com_basedir`.

## Donor issue retained as an audit item

The pinned donor's server setup writes generated private DER credentials into
`com_basedir`, which can be an installation or runtime archive directory. That
is unsafe and fails on read-only installs; this adapter removes those writes.
The donor also permits a weak `rand()` fallback after OS random failure; this
adapter fails closed. Review the eventual core broker UDP parser especially
carefully because its authenticated-gate behavior is security-sensitive.

## Verification

Final GnuTLS-enabled strict native build and the local fixture pass. Portable
OpenSSL and Windows configurations are qualified in the final platform build batch. It
also checks the side-effect-free room/URL address classifier and proves
`udp://` payload exchange,
direct `dtls://` transport acceptance and payload exchange with the pinned local fingerprint
when a DTLS provider is present, an encrypted `ice_ccand` control frame through
the same port, and a forwarding wrapper that proves it reaches the real native
broker-control parser. It also covers cancellation, listen/rebind/close, direct
reconnect, bounded server-queue retirement, and original `connectaddress`
retention. It does not complete full serverinfo/spawn/begin signon, prove a real
broker room exchange, NAT traversal, browser interoperability, or headset
behavior; those remain manual follow-up.


A separate actual dedicated-server and `udp://` client process completed private
serverinfo/spawn/begin signon, selected predictive movement and firing. This is
additional evidence beyond the payload fixture. Direct clients are explicitly
marked and bypass `QICE_UpdateBroker`; room clients still poll their configured
broker. No external broker, NAT, browser or physical headset was used.
