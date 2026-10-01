# C02 remaining publication: resolved before-code plan

2026-10-01. Authorized bounded implementation following the
[local Astra disposition](metadata-publication-integration-2.0-review.md).
Known2.0 checkout only. Main/master and user-owned migration-2.0.md untouched.
No tests/builds/compiler/lint/syntax probes/fixtures/scripts/SSH/game runs until
all required implementation is finished. Source reads, wc and scoped diff check
only. Keep all changes unstaged for main source review.

## Behavioral reference and architecture

Use pinned primary51b452c0 and QSS-M03a498aa metadata syntax/native signon,
and current project8192-byte stores/validated directional reader declaration.
Retain native reliable buffer/transport, Info_SetKey/Info_Enumerate consumers,
scoreboard, QC interception and lifecycle. Replacement networking, copied
metadata snapshots, persistent field cursors, generation registries, additional
queues and receive transaction layers are rejected: existing owners can carry
whole units and retry from committed stores.

One worker owns exactly Quake/server.h, sv_main.c, client.h, cl_main.c, cvar.c,
host_cmd.c, host.c. Existing source helpers may be reused in place. No new module,
renderer, protocol syntax or packaging edits. Target450–600 total changed lines
(additions plus deletions), stop/reopen before650 or any additional queue/owner.
Do not minify, move work outside the count, or weaken guards to meet the bound.

## Whole-unit projection and native publication

1. Add one serverinfo-pending flag and16 userinfo dirty bits to recipient state.
   Reset/rearm through connection/serverinfo/spawn/drop owners. Current stores
   remain authoritative; temporary serialization/preflight scratch is not kept
   across frames. Incremental success cannot discharge an existing full obligation.
2. Share public projection for full and incremental server/user info, including
   direct server-cvar mutation without SV_UpdateInfo redirect recursion. Enumerate
   losslessly, exclude underscore keys, retain star keys, preserve native quoted
   LF, omit quote-unrepresentable fields and delete previously public invalid
   replacements. Mutate local stores natively; publish actual committed values
   after possible native rejection/removal, never a requested va prefix.
3. Userinfo projection includes authoritative current native name/colors, inactive
   slots empty/zero. Preflight actual ordered receiver name/topcolor/bottomcolor
   replacements, every intermediate and final store, preserving representable
   custom fields. Quote-containing names may require binary companions, with
   explicit capacity accounting; do not discard valid fields to force a fit.
4. Measure opcode/NUL/slot/framing plus native scoreboard companions. Full
   serverinfo costs L+22; userinfo plus native name/colors L+18+d+N. Recipient
   message.maxsize governs fit. Declared readers: full token8191/text8211;
   incremental text2047. Unknown peers token1023/text2046. Prefer full; where
   individual limits prevent a full, an empty full plus every ui/svi may be one
   atomic reliable bundle only when all tokens/text/intermediate stores/total fit.
   No partial append, persistent field traversal, quoting escape or new syntax.
5. Distinguish backpressure from impossible-at-empty-buffer. Temporary pressure
   retains dirty state. Permanent incompatibility is handled only at the actual
   per-recipient native sender, with bounded diagnostic and disconnect headroom,
   correct host_client and immediate continuation after native drop. QC/cvar
   callbacks must not drop recipients recursively or terminate the server.

## Native lifecycle integration

6. Initial serverinfo drains before signon2 at PRESPAWN_SIGNONMSG, after ordinary
   precaches. After Send_Spawn_Info's clear and spawn QC, rearm serverinfo and all
   slots. Host_Spawn enters one added native sendsignon drain phase; append3 only
   when required units are appended and two bytes remain, then FLUSH. Persist
   drain across reliable sends; retain DONE suppression. Fastload only rearms.
7. Flush latest dirty stores in the existing sender while respecting native
   datagram failure exits and overflow/drop handling. Retirement QC sees old
   info; afterward clear it and mark the retired slot to recipients. Reuse
   publishes latest occupant. Existing native frags remain unchanged.
8. Record valid fullserverinfo receipt separately from nonempty data in cl;
   native state cleanup resets it. Initial client publication admits receipt OR
   nonempty serverinfo, preserving empty snapshots and updates-only peers.

## Client reliable staging and complete initial reply

9. Allocate cls.message NET_MAXMESSAGE once in CL_Init, immediately logical1024.
   Native disconnect/new connection/map cleanup resets logical1024 and pending
   reply; normal post-send clear retains selected size. After accepted header,
   CL_SignonReply1 selects NQ8192/Fitz32000/RMQ or accepted FTE264000, bounded by
   allocated capacity. Do not reallocate or infer reverse capacity from QSMI.
10. Preflight native name/control commands and whole initial color+userinfo+spawn
    with conservative reverse token1023/text2046 limits, complete formatting and
    opcode/NUL headroom. Existing name/star exclusions stay native. Preserve
    name-before-prespawn and userinfo-before-spawn. One pending native signon reply
    reconstructs from current stores under queued-buffer pressure and retries in
    CL_SendCmd, allowing existing bytes to drain; retire after complete append.
    Permanent logical-envelope or individual-command failure is visible before
    partial initialization. No silent omission/truncation or new metadata queue.
11. Inspect connected live userinfo cvar/control paths while integrating their
    shared mutation boundary. If complete-command/backpressure handling cannot
    fit this reviewed design, stop with exact evidence and proposed smallest
    adjustment; do not leave an observed loss/truncation unreported or improvise
    another owner. Stock-QSS-M's1024 store is a separate external limit; no
    unrestricted external-peer guarantee is added.

## Handoff and deferred acceptance

Worker reports scope_done, exact files/count, source/diff verification,
state/helper interfaces, assumptions, risks, unresolved substeps, follow-up and
relinquished ownership. It is not alone; preserve concurrent packaging/docs and
other work. No commits/staging/subagents/branches/deploy/telemetry reads.

Main source review integrates only complete bounded work, updates canonical C02
receipts and retains unresolved findings. After all implementation: meaningful
project desktop/VR software initialization and live mutations, empty/updates-only
metadata, all16 slots, oversized/invalid/privacy transitions, native overlays,
logical-limit resets/downgrades, pending-byte pressure, both profiles, spawn/QC/
fastload/map/retire/reuse and actual native movement/scoreboard/skin consumers.
Mocks/helper counts alone do not certify observable behavior. User live headset
and performance trials remain outside the goal.
