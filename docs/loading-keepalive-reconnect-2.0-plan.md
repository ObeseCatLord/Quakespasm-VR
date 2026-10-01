# C04/C06 loading and reconnect correction plan

2026-10-01. Before-code plan. User-visible goal: slow remote resource loading
retains a connection, while missing required local content fails once and returns
to the menu. Existing parser, transport and reconnect owners remain authoritative.
All executable checks wait until full implementation is finished.

## Verified references and adapter choice

Primary51b452c0 calls CL_KeepaliveMessage after each model/sound precache and
uses a five-second cadence. Current cl_parse.c has a disabled legacy helper and
no loading calls. Copy the placement/cadence, not the obsolete drain assumption:
that helper consumes CL_GetMessage data, assumes all inbound packets are nops,
uses MSG_ReadByte without preserving parser state, and clears cls.message after
an unchecked reliable send. Real queued gameplay/control data must survive.

The smallest adapter is a separate one-byte clc_nop sizebuf sent through the
existing NET_SendUnreliableMessage at those main-thread boundaries. No receive
polling, no writes to net_message/msg_readcount/msg_badread or cls.message, and
no new queue/thread/socket. Main checked net_main.c: NET_GetMessage refreshes
the peer lastMessageTime for any received message; sv_user.c accepts clc_nop
without requiring a movement command. Thus a loading nop keeps the server's
receive timeout alive without taking ownership of incoming messages or pending
reliable commands. Reliable receive/ACK progress resumes through the ordinary
parser after loading; uninterrupted mid-load reliable ACK pumping is not claimed.

Use a static double send timestamp (existing monotonic Sys_DoubleTime owner),
five-second cadence, and guards for active local server, demo playback,
disconnected/missing netcon. A send result -1 uses existing Host_Error connection
failure handling; successful/zero sends remain rate-limited. Do not touch timeout
cvars or synthesize transport receive timestamps. Remove the disabled legacy
helper rather than retaining a second implementation.

C06 current missing-model path calls Host_Error; existing timed reconnect can
retry indefinitely on the same incomplete mod. Reuse primary's warning,
cancel/disconnect/loading-plaque/menu sequence, adapting the symbol to existing
CL_CancelAutoReconnect. Current CL_ParseServerInfo returns true to abort its
caller, unlike primary's convention: return true after cleanup. Do not call
R_NewMap/SpatialWorld_NewMap or continue precaching after the failure. No changes
to the reconnect state machine, successful signon or game-directory policy.

## Ownership and sequence

Exclusive production write set: Quake/cl_parse.c only. One Luna xhigh worker;
main source review/integration. All other networking files are released from
the earlier worker slice; audio and renderer planning are disjoint.

1. Replace the disabled keepalive with the bounded independent nop adapter.
2. Invoke it after successful model and sound precache operations, main thread.
3. Adapt missing-model cleanup/abort from the pinned primary.

Final software acceptance: deliberately slow remote loading with peers, retained
reliable commands and unread packet/parser state, local/demo guards, nop cadence,
transport failure cleanup, successful signon, missing map and missing additional
model, timed/non-timed reconnect cancellation and return to an unfrozen menu.
Individual resource loads can exceed the interval between boundaries: this
inherits the reference placement rather than introducing loader I/O callbacks.
If final qualification demonstrates that those boundaries are insufficient,
reopen the narrow loader/transport seam; do not invent a receive queue in advance.
No tests/builds/probes/fixtures/game runs in this slice.
