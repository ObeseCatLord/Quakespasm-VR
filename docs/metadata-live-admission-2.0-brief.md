# C02 live reverse publication: narrow reopening brief

2026-10-01. Main brief after the Luna worker stopped with zero edits under
step11 of c89e7eb7. Packaging remains independently active. Resolve this seam,
then update the coupled before-code plan; no tests/builds/probes/SSH/game runs.
Solo project, not a new networking system. Do not re-audit185 features or the
already-adopted server sender/capability architecture.

## Verified facts and unknowns

| Fact | Evidence/status |
| --- | --- |
| Live userinfo has no failed-admission obligation | [verified: source] Cvar_SetQuick cvar.c536 mutates cls.userinfo, then writes a va command without envelope checks. Host_Setinfo_f5025 mutates the store then Cmd_ForwardToServer forwards raw text. CL_SendCmd drains only already-queued bytes. Worker stopped before adding unreviewed retry policy. |
| Native reliable overflow aborts | [verified: source] SZ_GetSpace/common.c1635 calls Host_Error without allowoverflow. Larger one-time backing storage addresses aggregate staging, not logical current-buffer pressure or truncated va framing. |
| Current store alone cannot reproduce deletions | [verified: source/main inference] Info_SetKey removes a key for empty value; native reverse command is setinfo key/value. Host_Setinfo argc!=3 prints the table, not reset. No native reverse fulluserinfo/reset command exists at this handler. Enumerating current cls.userinfo cannot name a deleted remote field. A lone dirty flag plus current-store replay is therefore not a lossless generic live repair. |
| Both live routes already use native setters | [verified: source] Cvar_SetQuick is the common name/topcolor/bottomcolor route; console setinfo redirects registered USERINFO cvars, otherwise edits cls.userinfo. Server-side QC/setinfo interception must remain native. |
| Callback ordering matters | [verified: source] Cvar_SetQuick validates ROM/LOCKED/REGISTERED, updates string/value/default/changed flags, calls callback and PR_AutoCvarChanged, then updates USERINFO. Cvar_Set_f permits USERINFO flags on an existing cvar. CL_AvatarChanged179 can recursively Cvar_SetQuick the same var to normalize it; ordinary avatar is not USERINFO, but flags can make it one. PR_AutoCvarChanged6949 updates QC globals. Do not claim arbitrary callbacks cannot consume reliable space or normalize values. |
| Actual incoming mutation can reject insertion after removal | [verified: source] Info_SetKey984 removes first, then validates key/value/capacity. Publication must represent the resulting local state, not blindly requested text. Temporary bounded scratch to compute a prospective result is permissible; persistent copied state is not part of the approved design. |
| Project reverse envelope is not arbitrary large-token support | [verified: source/review] Reverse token1023/text2046 remains conservative; protocol-selected client staging caps at NET_MAXMESSAGE=64000. QSMI is directional. Pinned stock-QSS-M stores1024 userinfo independently of transport. |
| Ordinary pressure frequency | [unknown, measurement excluded] No performance/live testing run or needed for this policy decision. Do not justify a queue by hypothetical frequency or imply larger staging eliminates pressure. |

## Decision and main lean

Prefer complete visible rejection **before the local change and associated cvar
side effects** when a live update cannot be wholly represented/admitted in the
existing reliable buffer. Normal successful updates retain native command/QC
semantics. Disconnected local settings continue native behavior. This makes a
current failure explicit rather than inventing another reliable queue; it does
not claim that an unadmitted requested change eventually happens. Select exact
diagnostic/return and shared bounded formatting/admission seam for cvar and
console routes, including prospective committed value/native name/color forms.

Compare this with one live dirty flag/current-store retry. Main rejects that
candidate as specified because deletions cannot be reproduced without tombstones
or a reset protocol. A bounded changed-key/tombstone registry or copied-command
queue would be new persistent policy beside cls.message and is not justified
merely to avoid reporting existing finite-capacity refusal. A new reverse full
snapshot protocol is a larger compatibility change; do not add it casually.

Challenge the lean if precommit admission cannot preserve callback/native state
ordering without another owner. In particular determine the smallest way to
avoid a preflight-to-append capacity race caused by synchronous callbacks, and
to avoid publishing a requested value when the actual setter/callback chooses
another one. Consider existing reliable storage itself as the admitted command
owner, but do not assume enqueue-before-callback is equivalent for all native
callback paths. No general cvar transaction/rollback or reservation manager.

This is one technical seam, not an optional human preference. Required outcome:
complete successful live commands/deletions, explicit finite-envelope failure,
no local/remote divergence introduced by silently losing an admitted change,
no buffer overflow or va prefix, existing callback/QC owners retained.

## Review contract and bounds

Read-only local Astra/xhigh verifies before critique. Focus on actual source
cvar.c/host_cmd.c/cl_main.c/common.c plus the resolved publication disposition;
pinned primary/QSS-M only where useful. No packaging, full inventory, tests,
builds/compiler/lint/syntax probes/fixtures/scripts/SSH/games/telemetry/branches/
nested agents. Return <=1400 words: prioritized disposition, smallest resolved
policy/order/interfaces, exact file-line evidence, genuine human decisions only
if necessary, state/behavior limitations and deferred observable cases.

Keep the existing seven-file write region if possible. Current target450–600,
reopen before650; advise revised aggregate estimate if this necessary seam
materially exceeds it. Worker has relinquished ownership with zero changes.
Clarify the typo in prior docs: accepted FTE2 means64000 bytes, never264000.
