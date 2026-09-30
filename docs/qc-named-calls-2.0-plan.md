# Named QuakeC calls on native dispatch

## Evidence

Primary `Quake/pr_cmds.c:6077` implements `callfunction` 605 by finding the
trailing function name and passing the function to `PR_ExecuteProgram`.
It advertises `FTE_CALLFUNCTION`; `isfunction` 607 tests name existence.
QSS-M and destination already remove the trailing name from `argc`, but
destination `Quake/pr_ext.c:4281` invokes QC bodies only. Builtin targets are
silently skipped. Destination `Quake/pr_exec.c:328` explicitly assumes a QC
body at executor entry; copying primary's unconditional entry would be unsafe.

Official FTE [generated interface documentation](https://github.com/fte-team/fteqw/blob/master/quakec/menusys/fteextensions.qc)
specifies the trailing name and forwarding the other arguments. Its
[named-call implementation](https://github.com/fte-team/fteqw/blob/master/engine/common/pr_bgcmd.c)
removes that name and enters the executor. Unlike destination, FTE's
[executor](https://github.com/fte-team/fteqw/blob/master/engine/qclib/pr_exec.c)
handles negative builtin statements explicitly at entry (around line 1801).
This establishes a real dispatch-boundary incompatibility, not a reason to
replace vkQuake's executor. Primary/QSS-M/FTE `isfunction` are existence tests;
they do not separately validate the target's VM permission.

Native `PF_Fixme` resolves a lazy builtin from the current call statement,
then binds the VM-specific extension handler. Simply calling an unresolved
builtin pointer from `callfunction` would let it infer the **callfunction**
declaration instead of the named target. This must reuse the existing binding
body with the actual target function supplied explicitly.

## Minimal adapter

Extract the existing native numeric lookup/bind/diagnostic/invoke body into a
private `PR_InvokeBuiltin(dfunction_t *fnc)` in `Quake/pr_ext.c`. Preserve
native VM-specific handlers, documented diagnostics, lazy table caching,
stub and disabled-query invocation policy. The helper invokes an already-bound
handler directly; otherwise it uses the existing extension registry. Validate
the target's numeric range before indexing, and avoid signed negation overflow
when deriving a number from a negative statement.

`PF_Fixme` retains its existing call-statement inference and delegates that
function to the helper. `PF_callfunction` keeps native ordinary-QC entry but
dispatches negative statements through the same helper. Save/restore the
caller's argument count, exclude the trailing name from the target count, and
leave argument data/return data in the native VM locations. Preserve the
native no-op for missing arguments/names/unmapped zero declarations. Keep
`isfunction` as the inherited/native existence query; it does not grant
permission or promise that an unsupported builtin can execute.

After both target classes are implemented, add primary's `FTE_CALLFUNCTION`
advertisement through the existing protocol-independent capability table and
its normal disable/override policy. No new registry, dispatch state, public VM
API, executor-entry behavior, opcode implementation or networking layer.

Replacing the executor with FTE's is rejected: its multi-program state and
resource model are unnecessary here. A second builtin lookup in the named-call
wrapper is also rejected because it would duplicate native lazy-binding policy.
Normal opcode calls remain on their existing direct dispatch and `PF_Fixme`.

## Scope and acceptance

Write only `Quake/pr_ext.c` and this/index compatibility documentation on `2.0`.
Plan precedes production. Source review must cover unchanged normal lazy calls,
known/unknown/out-of-range targets, already-bound/core/lazy extension targets,
CSQC/SSQC exclusions, inherited conflicting-number adapters, stub diagnostics,
nested named calls, argument forwarding/restoration and return preservation.

Builds/tests/probes remain deferred until all implementation is finished.
Final Linux/ARM qualification must invoke real QC bodies and numeric/named
core/extension declarations, including lazy first calls and rejected VM
permissions; verify scalar/vector/string returns, zero/seven forwarded
arguments, nesting, absent/zero targets and query disable/override behavior.
This is source adaptation, not runtime parity evidence.
