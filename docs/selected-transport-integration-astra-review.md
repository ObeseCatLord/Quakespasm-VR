# Selected transport/API integration: Astra disposition

2026-10-04. Review ran with `gpt-6-astra`, `xhigh`; main verified those effective
settings through read-only local metadata limited to that agent's model and
effort fields. No raw operational telemetry is exported. Review was source-only;
final builds/tests remain deferred until implementation finishes.

Main spot-checked the load-bearing claims in `QICE_Closed`, `NQICE_GetMessage`,
the global client FIFO, reliable assembly/ACK ordering, borrowed shared wrappers,
the old broker DTLS contexts, and the donor direct DTLS acceptance callback.

| Recommendation | Disposition |
|---|---|
| Keep module ownership until native qsocket teardown after ICE state destruction | Adopted for implementation: clear peer state, report terminal client failure or defer server retirement; do not clear the still-owned module prematurely. |
| Broker DTLS retains wrappers freed on listen/rebind | Adapted with the next finding: remove the duplicate broker session owner at the shared socket boundary. Existing ICE module owns all DTLS peers and its socket routing. |
| Two listeners cannot distinguish broker/game handshakes by ordering | Adopted: reuse the existing module handshake and dispatch successfully decrypted control/game payloads at the application boundary. Keep existing core broker parser/authenticated reply adapters. No new signaling protocol or handshake state machine. |
| Clear unread client FIFO when its owning connection ends | Adopted: retain the existing singleton-client contract with explicit close/failure cleanup and owner validation. |
| Check reliable capacity before ACK and retire on fatal assembly overflow | Adopted: match the native Datagram ordering; no damaged-stream continuation. |
| CSQC retains/restores native scratch and contacts correctly | Accepted after inspection. The demonstrated server-teleport bookkeeping leak is fixed by the narrow active-server-edict gate. No second prediction owner is justified. |

The bounded implementation worker owns the ICE adapter and factored donor
helper. Main owns core connection/Datagram/build integration. Required final
proofs: local reliable/unreliable exchanges, direct encrypted signon and broker
controls through the same port, error/reconnect/queued-message isolation,
listen/rebind closure, and fatal reliable overflow. External NAT/browser/headset
tests remain separate and are not inferred from local results.

## Optional native Windows provider

The existing MSVC project imports `ice.props`; absent `IceOpenSslRoot` retains
native UDP-only behavior. To enable ICE, supply a native x64 OpenSSL SDK root,
optional `IceOpenSslLibDir`/`IceOpenSslDllDir`, and `IceOpenSslLicense` for the
exact bundled provider. The property sheet adds the same twelve donor C files,
disables PCH only for those independent files, retains compiler warnings, and
requires/copies SSL+crypto DLLs and the provider notice. The matching provider
source must accompany redistributable builds. This configuration has not yet
been compiled; the available Windows builder has no discovered OpenSSL SDK.
