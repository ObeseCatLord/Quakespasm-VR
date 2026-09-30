# Inherited server player colors

## Verified brief for local Astra senior review

Solo-maintained engine migration; reuse native owners and keep this an SSQC
builtin adapter, not a color protocol/service rewrite. This brief preceded
production; the disposition below defines the implementation.

| Fact | Evidence / status |
| --- | --- |
| Primary SSQC `setcolors` 401 sets an active client's packed byte/team and broadcasts `svc_updatecolors`, without userinfo/name changes | [verified: primary `Quake/pr_cmds.c:1709`, registry at 6551] |
| QSS-M already adapts this builtin with two `SV_UpdateInfo` calls for top/bottom nibble strings | [verified: `QSS-M/Quake/pr_ext.c:2609`] |
| Destination slot 401/name is absent; native `SV_UpdateInfo` already sends userinfo keys to PEXT2_PREDINFO clients and legacy color updates to others | [verified: destination `Quake/pr_ext.c` registry and `Quake/cl_main.c:2742`] |
| Destination and QSS-M clamp server color nibbles 14/15 to 13; destination modern client userinfo decoding uses raw nibbles without that clamp | [verified: destination `Quake/cl_main.c:2680,2709`, QSS-M `Quake/cl_main.c:7031`] |
| Any native userinfo update decodes names too; missing userinfo name becomes `unnamed` | [verified: destination `SV_DecodeUserInfo`] |
| Native bot creation can leave name `unconnected` and empty userinfo | [verified: destination `Quake/pr_ext.c:2453` calls `SV_ConnectClient`; `Quake/sv_main.c:3299` resets the client and initializes that name] |
| Native `Info_SetKey` removes a key before attempting its replacement and returns no success code; failure can leave the key absent | [verified: destination `Quake/common.c:984`] |
| Native host name/color commands already use `SV_UpdateInfo`; changing their decoder/policy is unnecessary for an added builtin | [verified: destination `Quake/host_cmd.c:3230,3389`] |
| Exact end-to-end client behavior | [unverified: all builds/tests/probes deferred until implementation finishes] |

Paths above are sibling repositories under the workspace. Primary/QSS-M/vkQuake
are read-only; only `quakespasm-2.0` branch `2.0` may be edited. Production write
scope is `Quake/pr_ext.c`, plus the reviewed local-prefix correction in
`Quake/cl_main.c`; existing server/userinfo/renderer owners stay intact.

## Proposed adapter and open decisions

1. Copy QSS-M's two native top/bottom updates, retaining primary's active-client
   guard. Validate the raw VM entity reference before dereferencing it; world,
   nonclient/inactive/out-of-range references and missing arguments do nothing.
   Reject nonfinite packed color inputs; truncate and reduce finite values
   modulo 256 before conversion, matching primary's byte masking for defined
   integer conversions without overflow-dependent behavior.
2. Current lean: canonicalize each nibble 14/15 to 13 before updating userinfo,
   so native server/team, legacy clients and modern userinfo clients see the
   same native-valid color. This deliberately keeps native vkQuake color range
   rather than primary's raw 14/15 byte. Alternative: copy QSS-M verbatim,
   which preserves raw userinfo but can show different modern/legacy colors.
   Rejected: a second raw-color owner or changing native desktop decoders to
   enforce primary's full palette everywhere.
3. Current lean: if the target's userinfo name is missing, seed its existing
   client name through `SV_UpdateInfo` before color updates. This prevents a
   color-only call from renaming an ordinary new bot to `unnamed` and provides
   the name to modern clients before their per-key updates. Existing nonempty
   userinfo name remains authoritative as in native behavior. Alternative:
   accept QSS-M's name coupling verbatim. Rejected: restoring name fields after
   decoding, bypassing broadcasts, changing global name policy or mutating
   the bot creation path solely for this builtin.
4. Because native info insertion has no result, preflight the optional name
   and both colors sequentially on one temporary copy of that client's
   userinfo using native `Info_SetKey`/`Info_GetKey`. Verify each stored value
   before publishing anything. Failed capacity/invalid-name insertion leaves
   production state/messages unchanged. The temporary copy is not a second
   replicated owner; successful commits still use only `SV_UpdateInfo`.
   Alternative: omit preflight and accept native partial updates. Keep only
   the safety rails supported by actual native failure behavior.
5. Register SSQC-only slot 401 with no invented DP_SV_CLIENTCOLORS claim (that
   extension also implies a writable entity field, not just this builtin).
   Correct `spawnclient` documentation to point at this builtin without
   claiming the separate client-name feature.

Overlap: canonicalization and name seeding are both consequences of using the
existing userinfo owner; merge/simplify them if that avoids unnecessary policy.
Review depth: verify these boundaries, rank any real findings, recommend the
smallest complete adapter. No wider VM, network, foveation or graphics review;
no delegation, edits, tests, builds, compiler or runtime probes. No human taste
decision appears necessary under the user's native vkQuake desktop/graphics
and modern QSS-M-style networking preference; identify one only if substantive.

## Acceptance to run at the end

Bounded source acceptance must cover slot/VM permissions, validation, native
userinfo ownership and publication ordering. Deferred Linux/ARM software
checks must exercise actual SSQC invocation for active real and bot clients,
legacy plus PREDINFO recipients, all legal palette values, 14/15 policy,
negative/fractional/large/nonfinite values, name-less bots, absent/unknown
clients, exact numeric/named binding, failed info capacity and ordinary desktop
name/color commands. Confirm native team and both client presentations agree.
No headset testing or performance measurement is part of this slice.

## Astra design disposition

Main spot-check confirmed the load-bearing source findings before adoption.

| Recommendation | Disposition |
| --- | --- |
| Reuse QSS-M's two native updates with primary's active-client guard | Adopt. No new protocol or replicated color owner. |
| Canonicalize nibbles 14/15 to 13 | Adopt, explicitly retaining native-valid colors and aligning modern/legacy presentations. |
| Seed only a missing userinfo name from the current client name | Adopt. Existing nonempty names stay native-authoritative. Reject an empty seed. |
| Preflight native insertion before publication | Adopt one scratch copy, mirroring native unchanged-value skips and insertion order. No rollback/transaction framework. |
| Native prefix survives the entire recipient loop | Adopt QSS-M's stack `prestr[64]`. Native `va()` rotates through eight buffers; the old prefix was retained while each recipient allocated another buffer. This correction belongs to the existing `SV_UpdateInfo` owner. |
| Repeated accepted calls still set team | Adopt primary's unconditional team assignment after success, using the canonical bottom nibble. Also keep the native cached color byte canonical. Do not force synthetic info changes. |
| Advertise the actual builtin capability | Adopt `DP_SV_SETCOLOR`, verified in both donors, through the existing query table. Do not advertise unrelated `DP_SV_CLIENTCOLORS` or `DP_SV_CLIENTNAME`; their complete contracts are outside this slice. |
| No human decision or architecture replacement needed | Adopt. The native valid palette policy matches the user's native vkQuake foundation preference. |

Deferred acceptance additionally includes eight-plus PREDINFO recipients,
repeated identical colors after QC changes `.team`, and failed/unchanged-value
preflight cases. The review was read-only, with no builds, tests, compiler or
runtime probes. This disposition is design acceptance, not source/runtime proof.

## Source acceptance

Local Astra accepted the actual wrapper and native prefix correction against
the adopted disposition, with no actionable findings. Main inspected argument
validation before array/entity access, bounded conversion, sequential scratch
preflight, unchanged-value skips, native publication and unconditional cached
color/team restoration. The stack prefix is the copied QSS-M correction.
No builds/tests/compiler/runtime probes were run; the full matrix remains.
