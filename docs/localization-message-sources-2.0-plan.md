# Inherited message sources on the native localization owner

2026-09-30, verified design brief before implementation. Restore readable
campaign/mod text in desktop and VR by adapting the inherited message sources
to vkQuake's existing localization service. Solo maintainer; no new service,
filesystem mount, VM API or graphics work. User-owned migration status stays
untouched. Linux/ARM checks follow full implementation; Windows/live testing
and performance measurements remain deferred.

## Verified sources and unknowns

| Evidence | Consequence |
| --- | --- |
| Primary master `51b452c0`, `Quake/common.c:3918` parses `fgd/messages.fgd`; its loader parses the English table first, then FGD with add-or-replace semantics (`4060–4133`). | This inherited generic format is absent from destination `common.c`. Copy the parser/escape helper rather than invent another format. |
| Primary `LOC_GetMG3Fallback` (`4207`) and `LOC_GetString` (`4283`) provide English MG3 constants and bounded humanized `$mg3_`/`$QC_` text after raw lookup. MG3 is gated by the `fgd/quake_mg3.fgd` asset, not a directory name. | Preserve this inherited exception and generic QC fallback. No new mod names, gameplay exceptions or guessed strings. |
| Read-only PACK directory inspection of installed `mg3/pak0.pak` found `fgd/messages.fgd` (26083 bytes), `fgd/quake_mg3.fgd` and no `localization/` entry. | The missing parser is an actual data-format incompatibility, not only a hypothetical API mismatch. No pack content is copied into the repository. |
| Native `common.c:4480` `LOC_LoadFile` owns loading, in-place parsing, UTF8-to-Quake conversion, entries and the hash table. `LOC_Load` (`4819`) owns selected/system language and English fallback; `LOC_Init` installs its cvar callbacks. | Keep these owners, SDL2/3 and KPF paths, allocator and language features. Do not import the primary English-only loader. |
| Both engines' `PF_VarString` and `LOC_Format` already share localization/substitution and bounded output semantics. `LOC_GetRawString` is hashed native lookup. | No QC wrapper or parser replacement is needed. `LOC_GetString` is the narrow fallback boundary. |
| Primary separately discovers rerelease localization (`COM_LoadRereleaseLocalization`, `2455`); destination does not currently have that helper. | This additional inherited source remains stage 2, requiring reuse of the actual discovery boundaries before implementation. FGD/fallback integration does not certify complete MOD-006. |
| Selected non-English tables and FGD duplicate-key priority do not have an inherited primary reference: primary only loads English. | Explicitly preserve native translations when a non-English table successfully loads. English follows primary FGD precedence. This is the reviewed compatibility adaptation, not claimed exact primary behavior. |

## Minimal design versus replacement

**Lean:** append the copied FGD parser at `LOC_Load` after existing language
selection finishes. Keep its backing allocation inside the existing
`localization_t`; free/reset it with the existing load/shutdown lifetime. Copy
the primary escape and entry insertion helpers using native `Mem_*` and native
UTF8 value conversion. Factor the existing hash-build loop once and use it for
both native loading and the combined entries. No second dictionary, cache,
decoder, search path or message queue.

Rejected: replacing native loading with the primary English-only loader (loses
language/SDL3 behavior), eagerly mounting rerelease packs (imports unrelated
game data), or a separate per-mod translation service (duplicates state/policy).
The minimal new retained state is one FGD backing-text pointer and the inherited
MG3 availability flag, both under the native localization lifetime.

## Stage 1 contract and write set

Production: `Quake/common.c` only, target under 350 added lines. Main owns plan
and index; a single web coding worker owns the production file after disposition.
Do not expand filesystem discovery or edit the product branch/runtime assets.

1. Preserve native selected-language load and English fallback exactly. Track
   whether a non-English table actually loaded; it is not enough that the cvar
   requests one. Load optional `fgd/messages.fgd` through native `COM_LoadFile`.
2. Reuse primary quoted-key/colon/quoted-value, comment, escaped quote/backslash,
   newline and optional leading `$` parser behavior. Skip BOM within the FGD
   buffer without losing its allocation base. Convert values through the native
   UTF8 helper. Retain valid entries in the same native array/hash.
3. In English/fallback-English mode, FGD overrides an existing key as primary
   does. With a successfully loaded non-English table, preserve the pre-FGD
   entries; FGD adds missing keys. Later duplicate FGD definitions retain the
   primary last-definition behavior. One pre-FGD entry-count boundary suffices;
   do not introduce provenance state on every entry.
4. After raw lookup fails, reuse primary MG3 and generic QC fallbacks exactly,
   including missing-key identity for unrelated prefixes, bounded rings and
   explicit strings. Raw native/FGD translations always beat these fallbacks.
5. Every language reload and game switch clears old FGD entries/storage and
   recomputes the marker. No stale pointers/flags or doubled entries survive.
   Reuse the native hash build; omit rebuilding when FGD contributes no entry.

## Review decisions and acceptance

Use the senior-review workflow: local requested Astra Max verifies this brief,
then challenges ownership, FGD/language precedence, reload/UTF8 lifetime and
scope. Main records adopted/adapted/rejected recommendations before code.
Effective settings must be exposed for a certified skill pass; otherwise call
the result a requested-Astra source/design advisory. No human taste question
is needed for existing language retention. Do not re-review networking,
renderer, gaze, avatars or deferred stage-2 discovery here.

End-of-implementation software checks: actual mounted MG3 FGD; English override;
non-English successful and failed selection; translated and absent keys;
duplicate FGD definitions; comments/escapes/BOM/malformed lines; generic QC and
asset-gated MG3 fallback; empty/missing files; repeated language/game reload and
shutdown. Observe rendered/printed text through existing message/HUD consumers,
not just parser counts. Existing QC formatting and native language behavior
remain regression references. This stage is source integration until those
checks run; no compiler/build/engine/fixture/benchmark runs now.
