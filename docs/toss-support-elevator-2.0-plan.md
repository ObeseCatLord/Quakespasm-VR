# Toss support and legacy elevator corrections

2026-10-01, final checklist C08/C09 (MOD-011). Before-code plan. Main checked
current sv_phys.c:3279–3284 and11739–11749 against primary51b452c0
sv_phys.c:6767–6804 and4414; QSS-M03a498 has the same non-triggering relink.

Keep native SV_Physics_Toss, think_already_ran, pusher transactions and support
records. Robust support maintenance happens before Think and only for admitted
records; it does not replace post-Think validity. A second support manager or
physics solver is unnecessary. Expected scope is one copied small predicate,
one grounded branch, one non-triggering relink, all in Quake/sv_phys.c.

Copy the primary ground predicate at the Toss boundary: groundreference0 is
world support; refuse negative, misaligned or out-of-range nonworld references;
require a nonfree supporting edict with solid>=SOLID_BBOX. Check after Think,
including the caller's already-ran path. If invalid, clear FL_ONGROUND and
groundentity, then continue existing velocity/gravity/movement. Do not synthesize
touches, reacquire support or alter Think timing. Preserve finite native edict
storage assumptions; avoid an overflow-prone bounds product if the native
reference-to-index conversion can express the same contract directly.

At the successful non-robust legacy elevator DIST_EPSILON nudge, relink final
position with SV_LinkEdict(check,false) before continue, as primary/QSS-M do.
Do not change failed rollback, robust records or callback counts.

Source review only during implementation. After all implementation, consolidated
Linux/ARM acceptance must cover world/live/freed/hidden/nonsolid support after
Think and already-ran Think, invalid references, and successful/failed legacy
elevator nudges with final collision/linkage and no duplicated triggers. No
tests/builds/probes/game or deployed-state changes now. Reopen the design if
implementation needs another owner or unrelated pusher/Think restructuring.
