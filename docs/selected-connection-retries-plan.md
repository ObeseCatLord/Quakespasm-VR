# CAND-NET-003: ordinary nonblocking connection attempts

NET_DatagramConnectStart/Frame/Cancel already implement resolve/open/send/wait/
accept/retry/failure at net_dgrm.c:2482-2894. COM game-switch reconnect already
uses them in CL_AutoReconnectFrame. Datagram_Connect remains a synchronous
compatibility wrapper. CL_TryEstablishConnection is the remaining normal
blocking client entry (cl_main.c:651-671); NET_Connect also synchronously polls
LAN discovery. DNS resolution remains synchronous, matching the selected
QSS-M candidate; asynchronous DNS is not part of this bounded change.

Reuse cl_autoreconnect's connecting/wait_signon/idle lifetime with one direct
attempt flag, current host and compatibility dialect. Normal network connect
starts the existing datagram owner and returns; each host frame advances it.
No new socket or retry-policy owner. Resolve known cached server display names
through an existing/new narrow net lookup helper without synchronous slist.
Local loopback stays immediate. Empty-host discovery uses existing NET_Slist
polling across frames and selects exactly one result, rather than busy-waiting.

A direct pending attempt reports failures through existing menu error return
and ends loading plaques so menus/OpenXR continue. Existing disconnect,
mod-switch and shutdown cancel the socket owner. A native Escape/toggle-menu
hook cancels direct attempts only, not arbitrary automatic mod reconnects.
Expose CL_ConnectionPending for the existing menu owner; user cancellation
clears flags before disconnect to prevent reentry and stale completion.

Complete all selected implementation before consolidated native Linux checks;
reuse existing datagram/autoreconnect fixtures and add normal connect paths,
cached aliases/discovery, cancellation and successful local/remote signon.

## Native ordinary-connect fixture

`tests/ordinary_connect_native_fixture.c` has passed after completion of the selected source implementation batch. It links through
the assertion-enabled native Meson link recipe and calls the production `CL_EstablishConnection`,
`CL_AutoReconnectFrame`, `NET_CachedConnectHost`, discovery state, loopback
driver, and `NET_DatagramConnectStart/Frame/Cancel` owners. A localhost UDP
peer supplies only the wire-format control reply, leaving endpoint resolution,
socket allocation/release, request transmission, acceptance, and attachment in
the production code.

The fixture requires a licensed `id1` directory because it uses the existing
dedicated no-window engine bootstrap with UDP left enabled. The shared
`native_engine_fixture.h` loop wrapper remains in use; its `-noudp` bootstrap
helper is intentionally unsuitable because CAND-NET-003 must execute the real
datagram owner. The fixture supplies its own narrow dedicated bootstrap and
manually provisions only `cls.message`, which is the client resource the
connection owner needs when the dedicated Host_Init path skips CL_Init.

To repeat the bounded native check, run:

```sh
python3 tests/run_ordinary_connect_native.py --build-dir /path/to/debug/graph --basedir /path/to/licensed/game
```

The runner first links the fixture through the existing native graph helper, then
uses a disposable profile containing symlinks to `pak0.pak`/`pak1.pak`. It
requires `ORDINARY_CONNECT_NATIVE_PASSED` and preserves a log with
`--artifact-root NEW_DIRECTORY`.

Covered native paths are: an ordinary remote request returns pending before
the first frame and reaches transport acceptance; cancellation frees the
production qsocket and cannot attach a late accept; an unresolvable numeric
endpoint returns through the existing menu error state; a cached alias and an
empty-host discovery both return before a frame owns their progress; local
loopback remains immediate; and a server gamedir during direct wait-signon
releases the direct retry owner before `CL_ServerModDownload_Begin` takes over.

This is intentionally not complete remote-connected proof. The UDP peer does
not serve serverinfo, signon, game packets, or a real server process. A final
live two-process signon check remains required after the deferred batch, using
a production client binary and dedicated server with the same licensed assets.


Final native verification also passed a separate production dedicated-server
and ordinary client process on `e1m1`: complete private signon, selected
prediction, movement and firing. The ICE `udp://` form passed the same full-game
probe after a direct-client adapter fix. Direct peer polling bypasses room
broker work; broker clients retain their existing owner. Payload acceptance
alone was not used as signon proof.
