# C02: local metadata scratch capacity

2026-10-01. Independent before-code refinement of the adopted losslessness
finding in [the metadata senior disposition](metadata-publication-2.0-review.md).
The recipient-capacity question remains unanswered. This slice changes no wire
command, capability, framing, publication schedule, privacy policy or store.

[verified: main source] SERVER_INFO_STRING_SIZE is8192. Existing serverinfo,
server userinfo and client userinfo stores are8192 bytes. Info_Enumerate in
common.c retains the native delimiter walk/callback API but truncates each key
and value to1023 bytes. SV_UpdateInfo in cl_main.c reads old and committed values
through an oldvalue[1024] scratch buffer, allowing a prefix to stand in for the
actual stored value. Pinned QSS-M uses these same native helper algorithms; reuse
them rather than introducing a second registry, enumerator or parser.

Write set: Quake/common.c and Quake/cl_main.c only. Size Info_Enumerate's two
scratch arrays and SV_UpdateInfo's oldvalue array with SERVER_INFO_STRING_SIZE.
Keep their native walks, bounded copies, callback lifetimes and change dispatch.
All current Info_Enumerate callers supply the existing bounded stores, so their
individual fields fit these arrays without allocation or added ownership. The
extra stack scratch is bounded and these helpers are not renderer hot paths.

This does not certify lossless wire publication: native va uses a much smaller
scratch buffer, and recipient tokenizer/reader/reliable envelopes remain part of
the unfinished sender slice. Its serializer must preflight a complete command
without va truncation and distinguish temporary backpressure from permanent
oversize. No large-command capability may be inferred from PEXT2_PREDINFO.

Target six changed lines across the two existing functions; reopen before20 or
an API/policy change. Main source review/scoped diff checking only. No tests,
builds, compiler/lint/syntax probes or game runs until all implementation.
Final C02 qualification must check equal and distinct values sharing a1023-byte
prefix, longest native fields, local enumeration, committed-store updates and
the separately completed publication/privacy/recipient contracts.
