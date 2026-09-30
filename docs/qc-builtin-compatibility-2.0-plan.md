# Inherited QuakeC builtin compatibility

## Evidence and ownership

The local Astra registry audit found inherited MOD-001/004/005/006 ABI gaps
between primary `quakespasm-openvr/Quake/pr_cmds.c` and the existing vkQuake
registry in `Quake/pr_ext.c`. This is a source audit, not runtime acceptance.
The primary checkout is read-only; changes belong only on `2.0`.

Keep vkQuake's VM, builtin registry, lazy binding and resource owners. Adapt
demonstrated inherited contracts at those boundaries; do not import a second
VM or replace the donor registry. Occupied numeric slots must retain donor
semantics for donor programs. Any inherited conflicting binding needs verified
function-name, VM and declared-slot evidence before changing dispatch.

## First slice: successful extension discovery

Primary `PF_builtin_find` initializes the result to zero, compares names without
case sensitivity, and returns immediately after finding the registered number.
Destination `PF_builtinsupported` assigns a match but then unconditionally
overwrites the result with zero. Thus `builtin_find("sin")` returns zero even
though the existing extension registry maps it to 60 in both VMs.

Copy the primary lookup control flow into the existing destination helper:
initialize zero, reject an empty name, compare without case sensitivity, return
on the first match. Reuse the destination's assigned numbers and existing
extension table. This slice introduces no new entrypoints, permissions,
capability advertisements, number allocation or invocation policy. It does not
claim that core builtin discovery or the rest of the inherited ABI is complete.

## Remaining audit findings

These require handler and VM-contract verification before implementation:

- Core-name lookup is missing from `builtin_find`.
- Inherited numeric conflicts: SSQC `localsound` 80 versus donor `infokey`;
  EX-flags/path 90/91 versus `tracebox`/`randomvec`; EX-flags 430 versus
  `te_lightning3`; CSQC `dprint` 277 versus `frameduration`.
- Missing inherited registrations: `cvar_setf` 176; search 444-447; buffer file
  operations 535-536; SSQC `setcolors` 401; CSQC `drawline` 315, `cprint` 338,
  `sendevent` 359 and `getresolution` 608. Reuse existing cvar, filesystem,
  string-buffer, userinfo and draw services. Verify event transport separately.
- CSQC rain/snow 409/410 permissions differ from primary. Do not advertise
  support where the current VM has no handler.
- Numeric/name aliases differ: `strconv` 249/224; `draw_getimagesize`,
  `drawcolorcodedstring`, `drawcolorcodedstring2`; `ex_finalefinished` casing.
- Capability aliases and primary case/disable behavior differ. Existing
  handlers alone do not prove equivalent semantics or justify advertisement.
- Cursor/font 343/357 lose only inherited no-op fallbacks. Finale/EX-flags/path
  stubs are not new functional implementations; fog backend support was outside
  the registry audit. Do not promote these to functional claims.

## Acceptance

Use a bounded local Astra source review of the first helper change. Keep all
builds, compiler probes and runtime tests until implementation is finished, as
the user requested. At final software verification, exercise `builtin_find` in
SSQC and CSQC for registered names, mixed-case names, absent/empty names and a
named dynamic-number entry; verify that invoking returned numbers still follows
the existing VM permission/binding rules. Add core and collision cases when
their adapter slices are implemented. No assets or deployed game state change.

## First-slice source disposition

The helper change is implemented. Personal local Astra Max accepted it with no
introduced P1/P2: unsuccessful lookup retains zero, successful lookup returns
the assigned number immediately, and comparison follows primary's case behavior.
The reviewer verified existing documented/dynamic number initialization and
confirmed that allocation and VM invocation permissions are unchanged. No
builds, tests or probes were performed. The remaining audit findings above are
still open; this acceptance covers only the first slice.
