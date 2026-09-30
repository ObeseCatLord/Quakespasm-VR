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

## Local review and main disposition before production

The tool accepted an explicit `gpt-6-astra` / `max` selection. The reviewer
reported that effective model/effort metadata is not exposed, so this is advisory
Astra review with requested settings, not a certified effective Astra Max pass.
Main independently spot-checked the load order, two actual documented-slot
collisions, disabled early return, lazy activation and support-query branches.
No human decision or approval is needed for these authorized ABI repairs.

| Recommendation | Main disposition |
| --- | --- |
| Nine ordinary dynamic entries sharing one private error wrapper; null CSQC and `stub.` descriptions | Adopt. Reuse native number allocation/lookup, retain native `ex_draw_*`, and add no drawing capability. |
| Exact SSQC name/number adaptation through the existing all-functions scan | Adopt. All nine primary numeric declarations route uniformly to their dynamic error targets; native 81/82 and unknown declarations retain their meanings. |
| A narrowly named inherited-error VM category independent of the normal extension gate | Adopt. Recognized error fallbacks must not reach unrelated native handlers when disabled. Other SSQC extension and legacy adapter policies remain unchanged. |
| For this category only, also admit eligible empty named `#0` declarations | Adopt. Require zero `first_statement`, `parm_start`, `locals`, and a matching nonempty name. Preserve ordinary QC bodies and numeric-declaration eligibility. |
| Distinguish metadata discovery from callability | Adopt. `builtin_find` still returns registered numbers regardless of current VM availability; CSQC cannot invoke these SSQC handlers. Do not promise name invisibility. |
| `checkbuiltin` must remain false after this error handler is cached | Adopt as a narrow additional rail. Check this one private handler in the existing cached-handler branch; no general stub/support-policy rewrite. Post-error QC continuation has not been established. |
| General interpreter/name interception or global slot replacement | Reject. Duplicates working ownership or changes native meanings; cached direct calls make lazy-only interception incomplete. |

The proposed write set remains unchanged. Source-only review, main judgment and
the disposition above guide implementation; the end-of-goal review and actual
error-delivery/platform acceptance remain required.

## Implementation source checkpoint

The accepted adapter is implemented in the stated native owners. Local Astra
source review accepted all nine name/number pairs, dynamic SSQC-only registry
entries, nonrecursive copied error wrapper, category-specific disabled/empty
declaration handling, duplicate scan and lazy/cached false support reporting.
Main diff inspection confirms native `stof`, `multicast`, `ex_draw_*`, ordinary
QC bodies and prior adapter gates remain. Review retains the effective-setting
provenance limitation above; no builds/tests/compiler/runtime probes ran.
Actual error delivery and final Linux/ARM/migration qualification remain pending.
