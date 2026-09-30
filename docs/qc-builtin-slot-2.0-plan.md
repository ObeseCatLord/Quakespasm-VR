# Colliding inherited QuakeC slots: design brief

## Verified problem

Goal and scale: preserve the inherited generic QC contracts on the vkQuake VM
with a small adapter suitable for this single engine project. No new protocol,
dispatch service or VM is justified.

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

## Senior review environment and claim labels

| Fact | Evidence/state |
| --- | --- |
| Writable source | [verified: current commits] `quakespasm-2.0`, branch `2.0`; main owns edits |
| Behavioral source | [verified: source reads] sibling `quakespasm-openvr/Quake/pr_cmds.c` registry and handlers, read-only |
| Existing adapter | [verified: source reads] destination `Quake/pr_edict.c:PR_PatchRereleaseBuiltins` and call after `PR_EnableExtensions` |
| Dynamic allocation | [verified: source reads] `Quake/pr_ext.c:PR_InitExtensions` assigns numbers once; registry handlers are VM-specific |
| Invocation | [verified: source reads] `pr_exec.c` uses negative `first_statement` to index builtin table; `PF_Fixme` lazily activates registered handlers |
| Disabled extension behavior | [verified: source reads] `PR_EnableExtensions` returns early for SSQC when `pr_checkextension` is zero; it does not do so for CSQC |
| Inherited EX functionality | [verified: both handler reads] flags/path/finale return zero in both primary and destination |
| Third-slice runtime parity | [unverified] no implementation or runtime/compiler verification yet |
| Ambiguous unnamed declarations | [unknown] a colliding number without an identifiable name cannot establish intended contract |
| Unrelated user edit | [verified: git status] `docs/migration-2.0.md`; never edit or stage it |

Current leans: extend the existing adapter with destination-name lookup and VM
qualification; retain lazy binding and existing number allocation. Name discovery
and named `#0` SSQC localsound likely share the same alias boundary and may be
merged. Retain donor CSQC localsound 177. Keep general core-name discovery
separate; dprint's already-existing core number is sufficient for this conflict.
Preserve the SSQC disable boundary for new extension mappings; do not apply
ordinary QC bodies or guesses based only on numbers. Rejected alternatives:
global numeric replacement breaks donor calls; duplicate handler registration
and another binding table duplicate existing owners; program whitelists are
unneeded mod policy. These are proposed decisions, not verified conclusions.

Review depth budget: one personal local Astra Max design pass, <=800 words.
Verify load-bearing claims first, then rank the decisions and challenge the
necessity of any new owner. Give the highest-risk decision a concrete minimal
design. The not-list: no full VM/security audit, renderer, networking transport,
search/buffer implementations, unrelated registry gaps, builds/tests/probes or
nested delegation. Existing cvar/HUD adapters have separate source acceptance.
Return the critique as the final message; main will spot-check and record an
adopted/adapted/rejected disposition before production edits.

## Personal local Astra Max disposition

The review verified the existing owners and recommends the adapter with these
amendments. Main spot-checked the load order, first-name hash behavior, current
SSQC core 80, extension replacements/disable guard and VM-specific lazy handlers.
No human decision is needed; no implementation-wide acceptance is claimed.

| Recommendation | Disposition |
| --- | --- |
| Match exact name, declared slot and VM; never overwrite donor slots | Adopted. Extend the existing rerelease adapter's table with VM and optional canonical destination-name metadata. |
| Inspect every candidate for new cases because name hashes retain only the first duplicate | Adopted. New rows scan function declarations; existing three legacy rows keep their existing first-name lookup and semantics. This catches both EX-flags 90 and 430 declarations. |
| Merge SSQC localsound discovery and named `#0` aliases before binding | Adopted. One small normalization helper serves the existing discovery and zero-declaration loops. Preserve case-insensitive discovery, exact-case binding and all `#0` eligibility checks. Explicit SSQC 177 and ordinary bodies are not reinterpreted. |
| Expose existing assigned extension numbers with current-VM handler validation | Adopted. One narrow exact-name accessor; no new allocation, eager installation or registry. Lazy `PF_Fixme` targets are valid. |
| Preserve the exact disable boundary | Adopted. Gate new SSQC numeric remaps when extensions are disabled; leave discovery ungated and CSQC unchanged. Never choose 80 based on the current cvar: toggling it after load does not restore the overwritten slot. |
| Preserve load order/legacy patches and defer general core discovery | Adopted. Existing copy -> enable -> rerelease order remains. CSQC dprint remaps to native 25; no new general core-name table is required for this slice. |

Expected production scope remains the existing three files and one table owner.
The review adds the duplicate-declaration guard and resolves alias timing without
original-slot state or another binder. Final source review follows implementation;
runtime/software verification remains at the end of the full implementation.
