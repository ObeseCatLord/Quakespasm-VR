# C02 coupled publication: final senior disposition

2026-10-01. Local gpt-6-astra/xhigh reviewed the committed c48b81c8
[brief](metadata-publication-integration-2.0-brief.md), then assessed the main
thread's additional existing-buffer candidate. Effective routing verified.
Read-only review; no tests/builds/probes/game runs. This disposition supersedes
the brief's initial1024-byte aggregate rejection lean, not the directional token
limits established by68f8ec51.

| Recommendation | Main disposition and independent source check |
| --- | --- |
| Enlarge persistent client backing storage, preserve logical envelopes | Adopted with Astra's follow-up refinement. Main read SZ_Alloc/common.c1608: ordinary heap allocation, maxsize set only at allocation; SZ_Clear does not reset it. CL_Init3554 currently allocates1024. Allocate NET_MAXMESSAGE once, logical1024 before accepted header, then NQ8192/Fitz32000/RMQ or accepted FTE2 (64000) at CL_SignonReply1. Main read server selection3456–3513 and native socket send/reassembly405/657; this reuses compatibility defaults, not a new reverse capability declaration. |
| Reset limit and pending reply at native lifecycle owners | Adopted. Main read CL_ClearState433–451, CL_AttachConnection615–626 and disconnect clears. Map/new connection restore1024; accepted signon1 selects again. Normal post-send SZ_Clear must retain selected limit. No repeated allocation, copied snapshot, field cursor or second transport. |
| Whole initial reply plus current-buffer retry | Adopted. Main read CL_SignonReply1271 and CL_SendCmd3052: initial color/userinfo/spawn share reliable bytes; sender drains them normally. One pending signon-reply obligation reconstructs from current stores, appends only a completely measured unit, and clears only after append. Maintain native name-before-prespawn and userinfo-before-spawn order. Permanent token/command/logical-envelope failure is visible before partial initialization. |
| Preflight ordered native overlays | Adopted strengthened condition. Main read Info_SetKey984: removal precedes insertion capacity test. Received full userinfo is then overwritten by svc_updatename and colors at cl_parse3596 onward. Construct canonical authoritative fields where representable, and explicitly simulate/check name, topcolor, bottomcolor replacements and intermediate capacity, including quote-containing binary names. Final-size-only checks are insufficient. |
| Shared actual-store projection and cvar path | Adopted. Main read SV_UpdateInfo3268 and Cvar_SetQuick525: cvar currently publishes requested text after a possibly rejecting mutation. One direct mutation/publication helper avoids redirect recursion and uses actual committed data. Exclude underscore keys, retain star keys, omit quote-invalid fields/remove old public values, preserve quoted LF and local stores. |
| Native dirty obligations and spawn-drain phase | Adopted. Main read Send_Spawn_Info2403 clear, Host_Spawn3723 immediate3 and SV_SendClientMessages5325 onward. One serverinfo flag and16 slot bits per recipient; latest stores only, clear after complete append. Initial serverinfo precedes2; spawn rearms table/serverinfo and drains before3 through one added sendsignon phase. Fastload rearms without restarting signon; preserve DONE suppression and FLUSH behavior. |
| Atomic empty-full plus incremental reconstruction | Adopted as delivery atomicity only. Main read reliable EOM reassembly; no fragment becomes a client message. Existing callbacks still execute between commands and can do intermediate skin/movement work. Preflight all commands, ordered intermediate stores and the entire bundle; no cursor/chunk protocol or receiver transaction layer. Actual final consumers require deferred qualification. |
| Empty snapshot receipt plus updates-only compatibility | Adopted. Record valid full receipt separately from content in cleared cl state, and gate initial userinfo on received_fullserverinfo OR nonempty serverinfo. Do not make empty snapshots look absent or regress inherited updates-only peers. |
| Retirement and permanent failure at native sender | Adopted. Main read SV_DropClient607–646: disconnect append and QC precede native retirement. Mutation callbacks only mark obligations; actual recipient sender emits bounded failure with disconnect headroom, drops through correct host_client and immediately continues. Clear retired store only after QC, publish inactive empty name/zero colors or latest reused occupant, preserve native frags. |
| Unlimited stock-QSS-M aggregate support | Rejected as an inferred guarantee. Large reverse packets fit inspected transport allocations, but pinned QSS-M's server userinfo store is1024. PEXT2/QSMI does not enlarge its reverse tokens/store. Project-pair aggregate support is required; actual stock limits remain visible compatibility boundaries. |

Reverse tokens remain at most1023 bytes and command text2046, independently of
backing storage. Worst-case bounded initial setinfo framing is below40000; it
fits64000 without a new initialization engine. NQ8192/Fitz32000 can still have a
permanent aggregate failure. Do not imply the server-side reliable defaults are
an advertised reverse receive capability for arbitrary external peers.

Main accepts one coupled seven-file worker, target450–600 changed lines; reopen
before650 or another queue/protocol owner. The selected logical envelope can be
set in CL_SignonReply1, so cl_parse.c need not join the write region. Packaging
has separate ownership. Any connected unhandled live cvar/command publication
failure must be reported as a remaining C02 substep, not silently waived or
expanded into another queue while coding.

No human choice remains for required project desktop/VR cross-play. Deferred
observable cases: empty/updates-only initialization, aggregate above1024,
individual reverse limits, current-buffer pressure, NQ/Fitz/RMQ downgrade/reset,
all16 large slots, both profiles, intermediate overlay pressure, mid-signon
changes, spawn/fastload, retirement/reuse, QC setinfo interception and actual
scoreboard/skin/movement consumers. Source integration is not executable parity.
