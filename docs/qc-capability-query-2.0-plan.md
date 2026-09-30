# Inherited QuakeC capability queries

## Behavioral reference and verified evidence

Primary `quakespasm-openvr/Quake/pr_cmds.c` `PF_checkextension` returns false
when `pr_checkextension` is zero and matches capability names without case
sensitivity. Destination `Quake/pr_ext.c` currently uses case-sensitive names
and can return true with that cvar disabled. Adapt this query boundary to the
primary behavior while retaining native protocol checks and individual
`pr_ext_*` overrides. Direct builtin invocation, lazy binding, `builtin_find`
and server/client extension-loading policy are separate contracts and unchanged.

Three missing primary advertisements have existing destination handlers in both
VMs: `DP_QC_NUM_FOR_EDICT` (`num_for_edict`, 512),
`DP_QC_TOKENIZE_CONSOLE` (`tokenize_console`, 514), and
`FTE_QC_CHECKBUILTIN` (existing dynamic `checkbuiltin`). The entity conversion
matches primary; console tokenization uses the existing `COM_Parse` algorithm
and token offsets/lifetime, matching primary apart from native allocation.
Native `checkbuiltin` additionally recognizes lazily bound handlers and rejects
VM-unavailable entries and unbound described stubs. Preserve that handler.

Presence of a handler does not justify every missing advertisement:

- QSS-M `Quake/pr_ext.c` explicitly omits `DP_SV_POINTPARTICLES` because mods
  then assume DarkPlaces behavior and particles break. Preserve vkQuake's
  existing gated `FTE_SV_POINTPARTICLES`; do not copy primary's unconditional
  DarkPlaces alias without resolving this behavioral incompatibility.
- Native `PF_callfunction` removes the trailing name from `argc` and invokes
  ordinary QC bodies only. Primary passes any found function directly to
  `PR_ExecuteProgram`, whose entry assumes a QC body and cannot safely enter
  a negative builtin statement. Do not copy this unsafe dispatch or advertise
  `FTE_CALLFUNCTION` merely from the function's presence; verify the requested
  contract in a separate bounded slice. That subsequent
  [named-call slice](qc-named-calls-2.0-plan.md) is now source-reviewed: it shares
  the existing native binder and supports builtin targets without executor
  replacement. The historical warning above explains why a direct primary
  executor call was not copied.
- CSQC rain/snow handlers, setcolors, fog and cvar-description support are not
  established by this slice. The initial registry audit is a checklist, not
  evidence that these capabilities are complete.

## Minimal design and ownership

Use the existing `qcextensions` array and `PF_checkextension`. Initialize the
return value to false; return early for disabled queries or empty names; compare
names with `q_strcasecmp`. Keep existing protocol/particle predicates, canonical
lowercase override names and override defaults. Remove now-unreachable logging
conditioned on the globally disabled cvar. Add only the three verified aliases.

Replacing the registry or deriving all advertisements automatically would
duplicate donor policy and falsely equate callable handlers with full extension
contracts. No new capability owner, VM dispatch or renderer work is needed.
Production ownership is solely `Quake/pr_ext.c`, about three entries and one
small helper edit. Reopen the design if dispatch or another state owner becomes
necessary. Desktop and VR mods use the same query contract.

## Acceptance and remaining scope

Use a personal local Astra source review of the actual diff and reference
handlers. Builds, compiler probes and tests remain deferred until implementation
is finished. Final Linux/ARM software checks must cover both VMs, mixed-case and
empty/unknown names, all three aliases, global disable/reenable, canonical
per-extension overrides and unchanged protocol/particle rejection. Verify that
querying a capability does not eagerly bind or invoke a builtin. Direct call
policy is not changed or claimed to be disabled by this query guard.

Reenabling `pr_checkextension` restores capability-query answers. If SSQC loaded
while extensions were disabled, reload the program with extensions enabled to
perform skipped load-time setup, including `#0` binding. Disabling and reenabling
queries in an already initialized VM does not undo its mappings. Final checks
must distinguish these two startup/runtime cases; automatic recovery of skipped
setup is not added by this query adapter.

This slice closes these query differences only. Core builtin-name discovery,
other inherited calls/VM permissions and final software qualification remain
open; no whole-registry or runtime parity claim follows from source acceptance.

## Local Astra source disposition

Personal Astra Max reviewed the actual helper and three aliases. It accepted the
case/global guard, canonical overrides, preserved protocol predicates and both-VM
handler availability. Its initial P2 concern about startup-disabled SSQC was
reassessed after the main thread verified the loader's early return, sole caller
and absence of a cvar callback. The reviewer withdrew that introduced-defect
classification: capability availability and a particular program's skipped
load-time mappings are separate contracts.

| Recommendation | Disposition |
| --- | --- |
| Document the disabled-load versus initialized-VM reenabling distinction. | Adopted above and in deferred acceptance. |
| Gate the alias by scanning unresolved `checkbuiltin` declarations. | Rejected with the reviewer: this would suppress an available numeric handler because of incidental program contents and invent a special rule for a general native loading policy. |
| Add automatic rebinding/reload or another VM state flag. | Rejected as unnecessary for this query slice; preserve the existing loader and lazy binding. |

Final bounded source assessment found no introduced P1/P2 after this disposition.
No builds, tests, compiler probes or runtime trials were performed. The official
[FTE-generated interface](https://github.com/fte-team/fteqw/blob/master/quakec/menusys/fteextensions.qc)
documents `checkbuiltin` as a function-reference check for mapped/supported
builtins, especially named `#0` declarations; it does not establish automatic
reenabling of skipped destination load-time setup.
