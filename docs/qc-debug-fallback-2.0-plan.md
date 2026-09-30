# Inherited unsupported debug builtins: boundary decision brief

Decide the smallest loader/registry adaptation for nine inherited SSQC drawing
names. This is one engine's ABI boundary, not a new debug renderer. MOD-001
requires unsupported builtins to fail honestly rather than alias unrelated
native functions. Actual drawing is absent in primary and is not being added.

## Verified environment

| Fact | Evidence |
| --- | --- |
| Writable branch/source | [verified: commits/status] `quakespasm-2.0`, `2.0`; main untouched; user `docs/migration-2.0.md` remains dirty and excluded. |
| Primary behavior | [verified: source] readonly `quakespasm-openvr/Quake/pr_cmds.c:2634` has `PF_Fixme` calling `PR_RunError("unimplemented builtin")`; its registry assigns `draw_point` through `draw_cylinder` to 81..89 for SSQC only. |
| Destination core | [verified: source] `Quake/pr_cmds.c:pr_ssqcbuiltins` has `PF_Fixme` at 81..89. |
| Actual native collisions | [verified: source] destination `pr_ext.c:extensionbuiltins` has `stof` 81 and `multicast` 82. 83..89 have no documented extension targets; they already fail through native lazy dispatch. Do not describe all nine slots as occupied. |
| Existing adaptation owner | [verified: source] `pr_edict.c:PR_PatchRereleaseBuiltins` scans exact name + declared number for VM-filtered entries, after `PR_EnableExtensions`; it can resolve native dynamic registry numbers. |
| Existing fallback difference | [verified: source] native `ex_draw_*` entries use empty `PF_NotImplemented`. Preserve these native no-ops; primary non-`ex_` entries error. |
| Named/disabled behavior | [verified: source] primary binds empty `#0` declarations through its registry; native SSQC `PR_EnableExtensions` returns before `#0` binding if `pr_checkextension=0`, and existing SSQC adapter entries then skip remapping. Native lazy dispatch can still activate registered handlers with a warning when disabled. |
| Drawing/runtime proof | [unverified] no builds/tests/compiler/runtime probes; no debug drawing functionality in either primary fallback. |

## Worked proposal and decisions

Add nine SSQC-only, native dynamic registry entries for the exact inherited
names, all sharing one copied error wrapper. Mark descriptions `stub.` and keep
CSQC handler null, no capability advertisement. Add exact name/declared-number
entries to the existing loader adapter, pointing to those registered dynamic
numbers. Normal native numeric meanings and existing `ex_draw_*` remain.
Registry-only changes cannot redirect explicit `draw_point #81` / `draw_line
#82`; changing global slots would break native `stof` / `multicast`.

The subtle fork is disabled SSQC setup. Lean toward preserving these inherited
core error fallbacks even when extensions are disabled, using a narrowly named
VM-filter category in the existing adapter. It could also handle otherwise-empty
`#0` declarations for just this category, requiring the native zero
`parm_start`/`locals` eligibility. Do not broaden ordinary extension permissions,
ordinary QC bodies or existing rerelease remaps. A generic compatibility manager
or interpreter name interception is rejected: the existing loader owns this
boundary, and cached direct builtin calls would bypass lazy-only interception.

Alternative: retain the existing SSQC extension-disable gate. Simpler, but an
identified inherited drawing declaration at 81/82 can still alias a native
handler under that setting. Alternative: share a single private dynamic stub
with nine name aliases; fewer dynamic entries, but adds alias policy and loses
the registry's ordinary per-name metadata. Existing numbers are cheap and owned.
These are proposed choices, not accepted implementation decisions.

## Senior review contract

One personal local Astra at explicitly selected max effort: verify the facts
before critique; prioritize necessity/deletion, collision isolation, disabled
setup and named-call boundaries. Recommend the smallest complete adapter; merging
decisions is allowed. <=650 words, final response only, no edits/nested agents.
No renderer, unrelated QC APIs, protocol, build/test/runtime probes, or new
generic dispatcher. Main will spot-check load-bearing claims and commit the
disposition before production edits. Review does not certify the full migration.

Expected write set, if accepted: private error wrapper/registry entries in
`Quake/pr_ext.c`, existing adaptation table/filter in `Quake/pr_edict.c`, at most
the native empty-`#0` eligibility condition in that same owner. Reopen if another
state owner or general permission policy is needed. Final Linux/ARM checks must
cover all nine numeric/named declarations, native 81/82 in the same program,
duplicate declarations, real QC bodies, CSQC, native `ex_draw_*`, disabled setup,
discovery/support reporting and actual error delivery. All execution stays
deferred until complete implementation.
