# Portable server-physics saved-state initialization

2026-10-01. Both complete immutable b5e7af20 builds fail GCC13 -O3/-Werror
in sv_phys.c. This is a final Linux/ARM build correction, not a reopening of
excluded physical-contact melee functionality.

Main read the complete save/cleanup boundaries of both warned melee helpers.
SV_VRDwellBerserkPhysicalOutcome copies globals, call arguments and entity angles
before setting context_saved=true; cleanup restores only under that flag and
matching VM/program/global owners. SV_VRDirectMeleeOutcome likewise copies angles
before setting the flag and additionally checks the owned live edict at restore.
The private PlayerPostThink boundary captures movement angles only under shared_qc
and restore_qc_angles; restoration uses those same conditions plus QC/edict
liveness and unchanged authored angles. No uninitialized restoration is established
by this source review; the compiler warnings remain required build failures.

Smallest correction: initialize only the warned saved locals at declaration:
Dwell saved_globals to {0}, both helper saved_angles vectors to {0}, and the two
PlayerPostThink movement-angle vectors to {0}. Keep all actual snapshot assignments,
callback/ownership guards and conditional restoration. Do not initialize from an
unvalidated edict, suppress warnings, change QC angles/teleports, remove helpers or
introduce another state owner. No rewrite or new fixture is justified.

One Luna xhigh worker owns only Quake/sv_phys.c, maximum20 added/deleted lines;
stop/report if that does not suffice. Other workers own metadata tests only;
main owns independent pass diagnosis and documentation. Main reads the complete
patch, commits only this source/plan, rebuilds the preliminary host and performs
an isolated GCC13 compile against already-built pinned dependencies to reveal
further engine warnings without rebuilding every dependency. That diagnostic
is not complete package acceptance. Full immutable Linux/ARM builds and actual
QC/prediction acceptance remain required by the final checklist.
