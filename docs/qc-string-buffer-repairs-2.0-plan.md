# Native QC string/buffer wrapper repairs

Date: 2026-09-30; `2.0` only. These four wrappers in `Quake/pr_ext.c` expose
advertised native QC functionality in both VMs; retain their registry, permission
and temporary-string/buffer owners. A bounded local Astra gap audit identified
three concrete defects, which main verified against source before this plan.

| Trigger | Current defect | Copied/adapted reference |
|---|---|---|
| Populate a string buffer, then `buf_cvarlist` | Freed pointer array remains non-NULL, so rebuild passes freed storage to `Mem_Realloc`, or empty-result deletion frees it again | Primary `PF_StrBufClear` clears the pointer after freeing; copy that assignment at the existing native clear boundary |
| `strireplace("a","b","aaaaaa")` | Pointer-size bound truncates output on 64-bit targets | Copy primary `PF_strreplace_internal` and its two thin wrappers using real `STRINGTEMP_LENGTH` capacity |
| `strncmp("abc","Xabc",3,0,1)` and case-insensitive equivalent | Second offset is calculated but unused; negative offsets map to end instead of zero | Adapt primary bounds/optional negative-limit semantics while retaining native comparison routines |

Behavior reference: readonly primary master `51b452c0`, `Quake/pr_cmds.c`
`PF_StrBufClear`, `PF_strreplace_internal`, `PF_strncmp`, `PF_strncasecmp`.
Destination has native `Mem_*`, `PR_GetTempString` and both-VM registrations.
Primary's old case-sensitive `Q_strcmp/Q_strncmp` are equality-only and return
-1 for every unequal pair. Do not copy those helpers over vkQuake's native signed
lexical ordering; only the supplied-offset, clamp and negative-limit contract is
required here. Native case-insensitive comparison remains unchanged.

Minimal implementation: clear one pointer; copy the capacity-aware shared
replacement helper (including empty search and partial final replacement); copy
offset clamps and use both offsets. Both replacement modes need the same helper
because the current sensitive mode has the same premature-reservation/oversized
replacement pointer-arithmetic boundary. No parallel string service, allocator,
handle namespace, registry rewrite or VM permission change is necessary.
Expected scope: one production file, approximately 80 replaced/adapted lines.

Sequence: committed plan before code; main implements native-boundary copies;
bounded local Astra source review and main disposition. `git diff --check` is
allowed; builds/tests/compiler/runtime probes remain deferred until all
implementation is finished. The user's migration status document is untouched.

Final Linux/ARM VM qualification: populated and empty cvar-list reuse/rebuild/
delete; retained VM ownership; exact cited comparison triggers, zero/omitted/
negative limits and negative/past-end offsets; full/partial/empty replacements,
case sensitivity, real temporary capacity and termination. Invalid numeric
argument conversion and whole-registry compatibility are not established by
this bounded repair; MOD-001 still requires broader final acceptance.

## Implementation and source disposition

`9b5c992d` implements the one-file repair. The replacement helper and thin
wrappers were copied directly from the pinned primary file. A bounded local
Astra advisory accepted the actual change with no actionable residual defect.
Main checked the load-bearing buffer lifetime, both-offset calls and exact
capacity/copy/termination formulas against the reference and native consumers.

| Review finding | Disposition |
|---|---|
| Freed array reused by cvar-list refill or later deletion | Fixed: NULL assignment precedes native refill/reset; original range/VM-owner checks retained. |
| Pointer-size replacement bound | Fixed by the copied shared primary helper; sensitive/insensitive wrappers keep native registrations and temporary owner. |
| Missing second offset and negative clamp mismatch | Fixed: both independently clamped offsets used; omitted/negative limit chooses full native comparison. |
| Retain native lexical ordering rather than primary equality-only helper | Confirmed adaptation; no common string-helper or dispatch rewrite. |

All three trigger outcomes are source-derived, not executed. `git diff --check`
passed. Both-VM availability and numeric registrations are unchanged. Effective
reviewer model metadata is unavailable; requested-Astra/Max advisory is not a
certified senior-skill pass. No builds/tests/compiler/runtime/probes/fixtures
were run; broader interface and end-of-implementation Linux/ARM checks remain.
