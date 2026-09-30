# Colliding inherited QuakeC slots: design brief

## Verified problem

The inherited primary registry and vkQuake use incompatible contracts at several
numeric slots. Destination already has the inherited handlers or fallback
handlers; changing global slot meanings would break vkQuake's existing mods.

| VM | Primary declaration | Existing donor slot | Reusable destination |
| --- | --- | --- | --- |
| SSQC | `localsound` 80 | `infokey` 80 | `ex_localsound` dynamic entry / `PF_sv_localsound` |
| SSQC | `ex_CheckPlayerEXFlags` 90 or 430 | `tracebox` 90 / `te_lightning3` 430 | same named dynamic extension / existing zero fallback |
| SSQC | `ex_walkpathtogoal` 91 | `randomvec` 91 | same named dynamic extension / existing path-error fallback |
| CSQC | `dprint` 277 | `frameduration` 277 | native core `dprint` 25 |

Sources: primary `Quake/pr_cmds.c` builtin definitions and actual destination
`Quake/pr_ext.c` registry plus `Quake/pr_cmds.c` handlers/core tables. These
are generic inherited ABI contracts, not mod identifiers. The EX-flags and path
entries remain fallbacks; remapping cannot claim their missing functionality.

Destination `PR_LoadProgs` copies native VM builtin handlers, then calls
`PR_EnableExtensions` and `PR_PatchRereleaseBuiltins`. The latter already owns a
name-and-declared-number adaptation table in `Quake/pr_edict.c`: rerelease
`centerprint` 90 -> 73, `bprint` 91 -> 23, `sprint` 92 -> 24. It changes a
function's `first_statement`; the interpreter continues using ordinary builtin
dispatch. Destination extension registration already owns dynamic numbers and
per-VM handlers. Primary initialization likewise uses one registry and binds
named `#0` declarations without another VM. Those components remain reusable.

## Adapter comparison and proposed direction

Prefer extending the existing rerelease binding adapter. Add only the known
name/declared-number/VM cases above, with fixed destination 25 for CSQC dprint
and existing registered dynamic destination numbers for SSQC entries. A small
registry lookup can expose assigned numbers to the existing loader; it must
verify that the target handler exists for the current VM. Do not allocate
another number range, copy handlers, override global donor slots, add program
hash gates, alter the interpreter, or introduce a second registry/state machine.

Alternative: expand `PR_EnableExtensions` into a separate compatibility binding
table/owner. That is mechanically possible but duplicates the existing
name-and-number adaptation policy. Prefer the established loader owner unless
source review demonstrates an actual incompatible lifecycle or ordering.

The smallest vertical is an inherited `localsound` declaration at 80 and a
donor `infokey` declaration at 80 in the same server program. Remap only the
identified inherited function to `ex_localsound`'s assigned number; leave donor
80 intact. Then cover the EX cases and CSQC dprint/frameduration pair through
the same existing owner. Ordinary QC bodies, unnamed/renamed ambiguous calls,
other declared slots and other VMs retain donor behavior. A numeric slot alone
does not identify its intended ABI.

## Open design questions for local Astra

1. Can the existing `PR_PatchRereleaseBuiltins` table be extended safely with VM
   filtering and a registered destination name? Preserve existing unconditional
   rerelease patches unless evidence requires a change.
2. Which minimal registry-number accessor can serve that adapter without
   changing discovery or invocation policy? Confirm initialization/order and
   target permission checks, including disabled extensions.
3. SSQC named `localsound #0` and `builtin_find("localsound")` currently refer
   to the donor CSQC entry 177. They must select the existing SSQC dynamic
   handler in the inherited contract, while CSQC keeps donor 177. Resolve this
   at existing name discovery/binding boundaries, avoiding a second registry.
4. Should supported canonical core discovery (`dprint`, then other inherited
   core names) be integrated in this slice or remain a separate verified table?
   Avoid claiming complete core discovery from one remapped call.

## Scope and acceptance

Expected production owners: `Quake/pr_edict.c` existing rerelease table/function;
`Quake/pr_ext.c` existing registry lookup and `#0` binding as necessary;
`Quake/progs.h` at most one narrow accessor declaration. No renderer changes.
This is a mostly worked design brief, not permission to implement a new VM.
Resolve the questions through personal local Astra Max review and commit the
disposition before implementation. Reopen if it needs another dispatch owner
or broader allocation/lifecycle changes.

At final implementation-wide software verification, load programs containing
both conflicting declarations in the same VM and verify native donor calls
remain intact, inherited numeric and named calls select the intended handler,
dynamic discovery agrees with actual binding, ordinary functions and unrelated
slots remain unchanged, and forbidden VM targets/disabled extension behavior
retain their existing policies. Include map reload and independent SSQC/CSQC
programs. Builds, tests and runtime/compiler probes remain deferred until full
implementation. Hardware/live multiplayer tests remain the user's checkpoint.
