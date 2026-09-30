# Remaining string/token source audit

2026-09-30; writable `2.0` only. Bounded local requested-Astra/Max read-only
advisory compared current string/token wrappers with primary `51b452c0`, after
the [three concrete wrapper repairs](qc-string-buffer-repairs-2.0-plan.md).
No additional actionable defect was confirmed in the scoped ordinary calls.
Main independently checked the one reported native capacity divergence.

## Scope and evidence

The audit covers `PF_strlen`, `PF_strcat`, `PF_substring`, `PF_strzone`,
`PF_strunzone`, `PF_str2chr`, `PF_chr2str`, `PF_strconv`, `PF_strpad`,
`PF_strstrofs`, `PF_strtrim`, tokenization, `argv` and token-offset accessors.
It does not certify the whole 512-interface registry, arbitrary numeric
conversion, every Unicode value, VM error behavior or runtime mod parity.

| Finding | Verification | Main disposition |
|---|---|---|
| Native tokenizer accepts larger tokens than primary | Current `common.h:COM_PARSE_MAX_TOKEN_SIZE` is 4096; primary `com_token` is 1024 and its parser rejects overflow. Both tokenizers reuse their engine `COM_Parse`. | Retain native wider parser capacity. No demonstrated requirement to shrink vkQuake's parser or add a second QC parser/overflow owner. |
| `argv` exposes a bounded copy, while offsets span the parsed source token | Both actual `PF_ArgV` implementations copy through `PR_GetTempString` using `STRINGTEMP_LENGTH`, rather than returning the token allocation directly. Current token start/end accessors describe the input span. | Retain native temporary-string policy and full source offsets; do not promise that the copied text reproduces a token longer than temporary capacity. This explicitly disposes the wider-input difference as native behavior, not legacy error-count parity. |
| Ordinary string/separator operations and negative token indices | Reviewer compared the stated handlers/reference helpers and established no additional concrete difference needing a repair. `strpad` bounds ordering differs but ordinary representable widths produce equivalent bounded output. | Preserve existing wrappers and native memory owners. No replacement string library or adjacent cleanup. |
| `strtrim` lacks a primary implementation/registration | Bounded primary lookup found none. | Preserve useful native vkQuake functionality; do not infer inherited parity or missing migration from a native-only function. |

For a 1024-byte ASCII token followed by ` b`, the reviewer derived primary
token count 0 versus native count 2, native `argv(0)` length 1023 and native
end offset 1024. These are source-derived outcomes, not executed results.
Native wider acceptance is an intentional retained improvement; the temporary
copy limit is a separate existing API boundary. Ordinary fitting tokens remain
the inherited compatibility target. This does not change any cvar, protocol,
handle ownership, token allocation/free pairing or permission advertisement.

## Qualification

No source change is needed for this bounded audit. End-of-implementation
Linux/ARM checks should distinguish parser capacity from temporary-output
capacity; include fitting and larger tokens, quoted/separator input, source
offsets, negative indices and repeated tokenization/temporary-string lifetime.
Do not change expected long-token counts back to the primary overflow behavior
without a demonstrated incompatibility and a new disposition.

Effective reviewer settings are not exposed, so this is bounded requested-Astra/
Max advisory evidence, not a certified senior-skill pass. No builds, tests,
compiler/runtime probes, fixtures, nested delegation or telemetry access.
MOD-001 and the complete migration still require broader software acceptance.
