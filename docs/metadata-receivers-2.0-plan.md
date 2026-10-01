# C02 independent receiver admission repair

2026-10-01. Bounded before-code substep of C02/NET-021 from the existing
[Astra metadata disposition](metadata-publication-2.0-review.md) and
[final senior enumeration](final-checklist-refresh-2.0-review.md). The stock-QSS-M
sender capacity question remains unanswered. This substep does not select its
policy, expand wire readers, publish metadata or close C02.

[verified: main current source] cl_main.c:3181/3192 full/update handlers call
atoi and test only slot<cl.maxclients; negative/malformed slots can access outside
scoreboard storage, argc is unchecked and full replacement uses strncpy without
explicit termination. Server command registration at3589–3594 already owns
dispatch. Native CL_UserinfoChanged owns name/colors/skin side effects.

[verified: main current source] Existing CL_ParseBoundedDecimal at3319 parses
only decimal digits with a supplied maximum and overflow checks. Reuse it via a
forward declaration before these handlers; no second integer parser/helper layer.
Fullserverinfo/svi already check arity and use native store updates, so leave them
unchanged. Native command quoting has a separate unresolved closed-quote boundary
within the larger C02 plan; do not rewrite tokenizer behavior in this substep.

Only writes Quake/cl_main.c:

- FullUserinfo requires argc3, nonnull cl.scores, positive cl.maxclients bounded
  by MAX_SCOREBOARD, and bounded decimal slot0..maxclients-1 before indexing.
- UserinfoUpdate uses identical storage/slot admission with argc4.
- Full replacement rejects length>=sizeof(sb->userinfo) instead of silently
  publishing a prefix; q_strlcpy gives explicit termination for accepted data.
- Preserve Info_SetKey and CL_UserinfoChanged at the existing actual receivers.
  Empty full metadata still clears native name/color state via the native owner.
  No new source/mode restriction, peer negotiation, queue, score storage or field
  normalization; ordinary server commands and desktop demos retain native dispatch.

Target20–40 changed lines; stop before60/new file/policy. Source review and scoped
diff checking only. No tests/builds/compiler/lint/probes until all implementation.
Final malformed/empty/near-limit publication, loaded peer and slot reuse tests
remain within C02 consolidated qualification. Remaining producer/privacy/quoting/
recipient-envelope work needs its own completed before-code decision.
