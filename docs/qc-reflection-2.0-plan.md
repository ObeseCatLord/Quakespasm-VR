# Inherited field reflection on native VM owners

Local Astra source comparison found three concrete defects in the existing
field-reflection path. Main verified these against destination loader and hash
implementation, and readonly primary `Quake/pr_cmds.c:5974..6030`.

1. `PR_LoadProgs` builds `fielddefs_map` with pointers to the program's field
   table. `PR_MergeEngineFieldDefs` replaces that table when adding engine
   fields, but retains pointers to the original array. `PF_findentityfield`
   subtracts the returned pointer from the replacement array: undefined pointer
   subtraction, with an invalid reflection index possible. The primary lookup
   instead refers to its current table. Keep native hash lookup and loader
   ownership; rebind the existing map values to the copied table before freeing
   any previous owned table. Preserve the map's actual duplicate-name winner
   rather than assuming first/last insertion semantics.
2. Native merging tags missing vector component definitions with
   `DEF_SAVEGLOBAL`; `entityfieldtype` exposes that internal flag. Copy primary's
   return-type mask while retaining native internal definitions and serializers.
3. Native `putentityfieldstring` parses without relinking; primary relinks server
   entities after a valid-index parse, with `touch_triggers=false`, regardless of
   the parser's success result. Copy that SSQC-only call to `SV_LinkEdict`, keeping
   `ED_ParseEpair`'s return and native zoned-string/entity-reference policies.
   CSQC and invalid field indices must not enter server linking.

No second reflection service, linear lookup replacement, parser rewrite,
extended-type downgrade or new field state. Retain native temporary strings,
map allocation/cleanup, duplicate resolution, entity layout and lookup owners.
Scope: `Quake/pr_edict.c:PR_MergeEngineFieldDefs` and two existing wrappers in
`Quake/pr_ext.c`; main owns edits on `2.0`. This plan precedes implementation.

Bounded local Astra source review must check map rebinding/order, type masking,
server-only no-touch linking and unchanged parse results. Final consolidated
Linux/ARM checks must load programs both with/without added engine fields, query
existing/appended/duplicate names, compare field indexes/offsets, enumerate
vector-component types, and write origin/bounds/solid through reflection with
observed spatial membership. Cover parse failure, invalid indexes, independent
SSQC/CSQC VMs and reload. No builds, tests or compiler/runtime probes until the
full migration implementation is finished.
