# F03 loaded core binding qualification

2026-10-01. Reuse licensed stock-program extraction/section assembly from
`cooperative_qc_program.py` and the existing offline native engine bootstrap.
Append only prepared QC declarations/functions/results to a temporary program;
preserve every original function/statement and licensed assets outside git.
Load normally as SSQC, and with native CSQC table into the actual cl.qcvm.
Execute appended QC using PR_ExecuteProgram, not direct C builtin calls.

Required: builtin_find random/case variants=7, dprint=25, ChangeYaw/changeyaw=49,
cvar_setlong=72, finaleFinished=79, unknown=0; existing localsound normalization
SSQC equals ex_localsound (>0, !=177), CSQC remains177. Duplicate named #0 random
binds to numeric7 and yields identical seeded RNG output via actual interpreter;
ordinary same-name QC body remains callable/unchanged. Named #0 dprint calls
native print, inherited CSQC277 remaps to25 while SSQC277 remains unchanged.
checkbuiltin observes named finaleFinished SSQC supported and CSQC rejected,
unknown and mismatched-case #0 rejected. Do not execute unsupported functions
except in a separately planned error-boundary case.

Repeat after actual PR_ClearProgs/reload; clearing CSQC must preserve SSQC
program/function/result state. Disabled SSQC load leaves new #0 declarations
unbound; enabling cvar live does not rebind; reloading enabled does. CSQC keeps
its native enable behavior. No alternate registry, program compiler, protocol,
512-builtin suite or full VM-resource ownership claim.

Worker ownership only tests/qc_binding_program.py and
 tests/qc_binding_native_fixture.c. Main owns docs/README/review/integration.
Strict existing negotiation_native.make in isolated engine, with actual current
cl_main.o, then run from disposable licensed profile. Unexpected mapping/error
requires evidence/report, never fixture weakening or production edits. This
closes only this handler-family subset of F03, not all loaded-QC requirements.

Main integration: stock pak precedence requires a separate `-game binding`
profile for augmented loose QC. Initial RNG equality failed because the
planned libc srand does not seed native COM_Rand; use the declared native
COM_SeedRand for both interpreter calls, retaining exact output equality.
These are harness/reference corrections, no production repair.
