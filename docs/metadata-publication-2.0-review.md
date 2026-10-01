# C02 metadata senior disposition

2026-10-01. Main synthesis of local Astra xhigh review of
[the decision brief](metadata-publication-2.0-brief.md). Main verified effective
gpt-6-astra/xhigh routing from model/effort fields only. No executable checks,
production edits or metadata implementation plan yet. This refines C02 within
the final checklist; it does not introduce another feature requirement.

| Recommendation | Main disposition |
| --- | --- |
| Do not treat Info_Enumerate as lossless over8192-byte stores | Adopt. Main checked its key/value1024 buffers and SV_UpdateInfo's oldvalue1024. Use bounded spans or a narrowly shared projection; do not truncate silently. |
| Do not infer8192 command support from PEXT2_PREDINFO | Adopt. Pinned QSS-M03a498 uses com_token1024 and MSG_ReadString2048. Its receiving scoreboard is8192, but that does not remove those upstream parser limits. Its server-side userinfo store is1024; distinguish the declarations correctly. |
| One pending serverinfo flag plus16 userinfo dirty bits | Adopt as a minimal candidate delivery design, pending capacity decision. Existing stores and native reliable owner suffice; no copied snapshots, generations, separate queue or parallel state machine. |
| Preserve initial publication before signon2 and spawn table before signon3 | Adopt. Main checked CL_SignonReply only sends initial custom userinfo when cl.serverinfo is nonempty, and Host_Spawn emits3 after Send_Spawn_Info. Use existing prespawn/spawn owner to drain obligations, preserve idle guard; fastload rearming must not restart signon. |
| Full userinfo followed by current native name/colors | Adopt. Main checked CL_UserinfoChanged recomputes scoreboard fields; deferred snapshots could otherwise overwrite server-clamped/restored native fields. Account for all accompanying bytes atomically, retain native frags. |
| Retire slot after ClientDisconnect; gate by recipient | Adopt. Current disconnect callback still needs old stores. Emit empty inactive snapshot or latest occupant on reuse from current state; no stale retirement payload cache. |
| Share public projection for full and incremental publication | Adopt. Main checked SV_UpdateInfo and cvar serverinfo bypass. Exclude underscore keys, retain star keys, preserve local stores, remove previously published fields when a replacement becomes unrepresentable. Literal quotes cannot be encoded by current protocol; native quoted parsing accepts LF, so do not call LF rejection an established encoding requirement. |
| Repair only actual full/update metadata receivers | Adopt. Reuse existing CL_ParseBoundedDecimal (forward declaration/placement needed), argc/range/storage/termination and closed-quote validation; extend only the explicit fui reader exception. No general tokenizer rewrite. |
| Send everything in one spawn message or indefinitely retry an oversized command | Reject.16 large native snapshots exceed64000 total, and NQ reliable8192 leaves8170 fullserverinfo payload bytes after22 bytes framing. Whole-command fit must distinguish backpressure from permanent incompatibility. |
| Public QSS-M compatibility/capacity outcome | Pending optional user clarification requested asynchronously: direct stock QSS-M interoperability versus same-build2.0 desktop/VR cross-play. Do not implement dependent recipient-size policy based on elapsed time or guessed capability. Other checklist work continues independently. |

Load-bearing main spot-checks: common.c Info_Enumerate/Info_SetKey/quoted parser;
cl_main.c CL_SignonReply, full/ui receivers and bounded-decimal helper; server.h
store/limits, native reliable/avatar retry family, host_cmd.c spawn clear/signon3,
and pinned QSS-M server/client/common declarations. The raw review's1024 userinfo
receipt referred to the server store, not the receiving scoreboard; its supplement
clarified that distinction. Parser limits remain the demonstrated incompatibility.

An implementation plan must settle the negotiated recipient envelope and explicit
oversize outcome before coding. Reuse public-compatible existing commands, native
stores, reliable sender and signon barriers. Do not add another wire dialect just
to avoid the capacity question. Final software acceptance still needs initial/
empty/near-limit publication, all16 slots, mixed peers/backpressure, mid-signon
changes, spawn/fastload clear, drop/reuse, native scoreboard agreement, privacy/
invalid transitions, malformed commands and movement serverinfo consumers.
