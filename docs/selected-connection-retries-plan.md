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
