# Enyo katana integration into the shared held-melee adapter

## Scope and reference

This slice extends the QBJ3 adapter from `d991b389` on branch `2.0`.
It adds the installed Enyo katana to vkQuake's existing alias rendering,
prepared contact geometry and direct-native-leaf stroke owner. It does not
complete the overall migration, the other mod melee families, or headset
qualification. An Astra senior review checked the combined implementation;
its findings and dispositions are recorded below.

The behavioral reference is the OpenVR donor's `Quake/vr_melee_qc.h`
(`SV_VRMeleeEnyoProgs`, readiness and outcome handling), its immersive katana
recipe in `Quake/vr.c`, and the reused `Quake/vr_mdl_split.h` splitter.
The installed `enyo/pak0.pak:progs.dat` is 823998 bytes, SHA-256
`b0d3865f1192b3858e7410ea31cbc82d88136e635b9c1d13d0aff9bbeddaeb1e`.
Its 60325 statements, 3607 functions and 9635 globals are pinned along with
the borrowed function signatures. These claims concern this revision only.

## Implementation and review dispositions

| Decision or finding | Disposition |
| --- | --- |
| A second sword renderer or contact state machine would duplicate the completed wrench path. | Reuse immutable held-model recipes, vkQuake's loader and alias matrix, the prepared two-point edge, and the existing per-hand stroke owner. No new scheduler or wire format. |
| Katana calibration differs from the wrench's centered, left-authored grip. | Katana retains source-model offsets and scale, mirrors in the left hand, and adds no controller roll. Wrench retains its centered grip and tracked-forward roll. Numeric tests cover both. |
| Native `hitsword` can install harmless hit aftermath. | Keep it intact. Admit only verified stand/run or sword10..17/swordhit1..5 when a think is scheduled; reject sword1..9, including overdue callbacks. Nonpositive nextthink follows the donor's unscheduled-state policy. |
| Shared stroke identity can no longer be a berserk boolean. | Use explicit NONE, QBJ3 wrench, QBJ3 berserk and Enyo sword integer subtypes; update reset and GDB fixture allocations/assertions. |
| Unsupported or unprepared geometry must not generate physical hits. | Require matching game, source, offered profile, generated provenance, skin/topology, ready pose and prepared frame identity. Contact uses the same prepared collision displacement as rendering. |

The Astra review found no confirmed Enyo-specific implementation bug or QBJ3
regression. Its recommendations were handled as follows:

| Recommendation | Disposition |
| --- | --- |
| Add a queued-command, two-victim Enyo proof; wrapper calls alone cannot establish full contact behavior. | Adopt: add `tests/vr_enyo_contact_runtime.gdb` with a disposable asset runner. Runtime execution remains pending under the recorded ptrace restriction. |
| Correct the sword readiness rationale. | Adopt: only installed sword4 calls `W_FireSword`; retain native ownership for every scheduled sword1..9 frame, as the donor does. |
| Correct the default katana edge-length calculation. | Adopt: include `vr_world_scale / 0.75`; the factory world scale is 1.0. |
| Preserve the shared generated-model, presentation and stroke owners. | Adopt: keep the narrow recipe and native-leaf adaptations, including both lifecycle validation points. |
| Remove unused `single_hand` output from the contact selector. | Adopt: remove the output and update all callers; defer further abstraction without evidence of duplicated behavior. |

`Mod_GetHeldMeleeRecipe` identifies the single held models. Generation stays in
the existing source/split loader, retaining source skin identity and excluding
enhanced-model substitution for the private mesh. Ready geometry is cached
while CPU pose data is available. `V_PrepareHeldMelee` loads and prepares the
normal alias entity before draw tasks, freezes katana frame 0 or wrench frame
10, and publishes the raw edge and collision displacement. Normal and
show-tris draws share `R_HeldMeleeMatrix`. Input computes effort from raw
tracked endpoints before adding that displacement.

The server advertises ENYO melee only when the exact program and existing
immersive-melee policy permit it. The client also requires its local option,
controller aim and matching selected model. Ordinary desktop rendering and
QC trigger firing continue through their existing paths. The independently
authorized QBJ3 native berserk pair remains separate from this option.

Enyo outcomes borrow function 347, `hitsword`, with the native entity/vector/
vector arguments and accepted trace globals. The first outcome sets attack
and switch recovery to QC float time plus 0.4 seconds and plays the donor's
sword swing cue. A whiff consumes recovery without damage or hostile marking.
Hits retain native damage, healing, effects and aftermath code rather than
reimplementing those policies. Enyo keeps the donor's effect origin four units
behind impact along forward; the QBJ3 brush-normal adjustment remains QBJ3-only.

The shared contact owner retains at most two distinct victims under the
original recovery deadline. It does not remove previously hit entities from
collision; retraces cover remaining motion. World impacts, whiffs and rejected
gestures consume the stroke. Native callback returns, including failures with
side effects, are followed by existing VM, program, edict/global storage,
owner, live subtype, body-origin and contact-continuity checks before another
state write, retrace or hand. No attack fan is replayed by the physical adapter.

## Verification and limits

The full Linux debug build passed with `ninja -C build-debug -j4`.
The production matrix/recipe fixture passed for both hands, pitched/rolled
wrists, nonzero gun pitch, source offsets, grip centering, winding, scaling and
invalid values. Existing locomotion and stale-input regressions passed; the
stale-input fixture used ASan/UBSan with leak detection disabled.

`tests/vr_enyo_melee_state.sh` passed against the installed program under
ASan/UBSan with leak detection disabled. It verifies hash/function admission,
scheduled/overdue attack rejection, valid aftermath, unscheduled states,
invalid time, QBJ3 readiness and shared state reset. It does not execute QC.

A read-only asset probe ran the production splitter and verified both complete
generated outputs: katana 344020 bytes / CRC32 `a707a071`, wrench 702244 bytes /
CRC32 `1bfff189`. The katana's ready-pose edge uses vertices 13 and 77. Its
unscaled length is 67.674399; the default 0.2 held calibration and factory
`vr_world_scale=1.0` add a `1/0.75` matrix factor, giving about 18.04651 Quake
units at unit gun/entity scale, below the server's 32-unit edge limit. This is
geometry evidence, not visual alignment proof.

The Enyo native-outcome and queued-contact GDB fixtures, and updated QBJ3 fixtures,
remain unexecuted under the recorded ptrace restriction. Their shell and
embedded Python syntax checks passed; syntax does not prove native damage or
complete contact processing. Full Enyo native damage/healing,
follow-up behavior, callback faults and client/network/contact integration
remain to be exercised. Headset alignment/feel and eye-tracking qualification
remain with the user; Windows and ARM checks remain deferred as requested.

The feature map keeps WPN-012 and MOVE-005 partial. Other mod contact families,
parry/reach/support-grip behavior, other program revisions, remaining avatar
work, measured performance goals and release integration remain part of the
overall migration.
