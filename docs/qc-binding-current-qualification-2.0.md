# F03 loaded core binding results

2026-10-01. Existing isolated native fixture objects through production482cd9f5;
current QC loader/registry/interpreter source is unchanged by the later renderer
repair4ef67790. Strict native DEBUG/SDL3 Make build and final fixture run exit0,
with QC_BINDING_CORE_NATIVE_PASSED. No native renderer/window/device runs here.

Two actual VM programs load through PR_LoadProgs with their native SSQC/CSQC
builtin tables; appended QC runs through PR_ExecuteProgram. All original licensed
program sections are byte-preserved (16 functions and56statements appended),
including version/CRC and entity-field width. No original QC is committed.

Observed discovery: random/mixed case7, dprint25, ChangeYaw/changeyaw49,
cvar_setlong72, finaleFinished79, unknown0; native SSQC localsound normalization
matches ex_localsound (>0 and not177), while CSQC keeps177. Appended duplicate
#0 random/dprint bind to native slots; identically native-seeded numeric/named
random calls return exactly the same value, and the same-name ordinary QC body
still executes/returns0.25. Inherited dprint277 remaps and executes at25 in
CSQC; SSQC remains277 (no unsupported invocation claim). Actual checkbuiltin
calls report SSQC finale supported/CSQC unavailable, unknown/mismatched-case
#0 unavailable. Permission errors themselves are not exercised by this subset.

Actual CSQC clear/reload preserves the live SSQC program/result, and the surviving
SSQC interpreter checks pass before its later reload. Disabled SSQC load leaves
new #0 declarations unbound; enabling the cvar live does not rebind them;
enabled reload does. CSQC binding remains enabled with the cvar off. These are
actual native loader/dispatch consumers, not direct C handler calls. No concurrent
file/buffer resource, named callback, entity search or gameplay claim follows.

Main reviewed both complete worker files. Before execution it caught/corrected
assembler function-table indices and CALL/STORE_FNC operands (global references,
not table indices), and removed an unnecessary initial server-program reload.
First execution used loose id1 QC hidden by stock pak precedence and correctly
failed the missing-global check; a separate -game binding profile loads it.
The next run exposed the planned libc seed mistake: PF_random uses COM_Rand,
so the native COM_SeedRand now seeds both calls without weakening equality.
A main patch command initially used the isolated directory with relative source
paths and failed before editing; corrected absolute paths and a fresh strict
build/final run pass. These are fixture/recipe findings, no QC production repair.

Private current evidence: qc-binding-current/{fixture,game/binding/progs.dat,
fixture-csqc.dat}, logs/qc-binding-current-build-final.log and
qc-binding-current-run-final.log. Earlier failed logs remain distinct. This
closes this F03 binding/permission/reload subset; the other frozen F03 families
stay open. Linux ARM/artifact refresh and final local Astra goal signoff remain
under F10. Main/reference/assets/user-owned migration document are untouched.
