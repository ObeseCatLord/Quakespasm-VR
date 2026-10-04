# CAND-NET-005: CSQC input history and client pmove adapter

## Evidence and decision

The pinned donor is `../QSS-M` commit `03a498aabc411e2e739adc815c5536b161b9626e`.
Its CSQC `getinputstate` builtin is `PF_cs_getinputstate` in
`Quake/pr_ext.c:8397`; builtin 345 publishes either the pending frame or a
matching `movecmds[seq & MOVECMDS_MASK]` slot through `PR_GetSetInputs`.
Its client/server shared pmove adapter is `PF_both_pmove` at
`Quake/pr_ext.c:1895`, which calls `World_AddEntsToPmove` before
`PM_PlayerMove(1)`. Donor builtin registration assigns 345 to CSQC and 347
to client/server physics (`Quake/pr_ext.c:8993`).

This tree already owns the relevant state: `cl.movecmds` is a sequence-tagged
ring and `cl.pendingcmd` is present; `PR_GetSetInputs` is implemented in
`Quake/pr_cmds.c`; client replay calls `PMCL_SetMoveVars`, `PMCL_AddEntities`,
and `PM_PlayerMove`; and `PMCL_AddEntities` already builds collision from the
world plus engine network entities. `PF_sv_pmove` builtin 347 already validates
a server entity then delegates to `SV_RunStandardPlayerPhysics`.

**Decision: reuse these owners.** A new prediction owner would duplicate
command retirement, snapshot selection, movevar selection, and the physent
list, and would diverge from native replay. This change must add only a CSQC
adapter over the existing journal/pmove/world helpers. Server builtin 347 and
server physics remain unchanged.

## Adapter surface

`pr_ext.c` exposes, CSQC-only:

* builtin 345 `getinputstate(float sequence)`: reject non-finite and fractional
  values before conversion, then delegate all sequence/window/tag/pending
  validity to main-owned `CL_GetCSQCInputState`. That helper returns either a
  tagged committed journal command or the donor-equivalent side-effect-free
  pending preview for `sequence == cl.movemessages`. The returned command is
  copied to the existing QC `input_*` globals.
* builtin 347 `runstandardplayerphysics(entity ent)`: validate current CSQC VM,
  entity alignment/range and writable entity storage; validate finite vector,
  timestep, and bounds inputs; then run the existing native `PM_PlayerMove`
  through a narrow saved/restored context helper. It updates only the passed
  CSQC entity. It never writes `cl.entities`.

The helper seeds collision with the existing `PMCL_AddEntities` result, then
walks the already-linked per-QCVM area tree to append CSQC-owned solids. It
does not create a second world, entity list, or prediction state machine. All shared
`pmove`/`movevars` fields are saved and restored on every exit; the helper
rejects nested entry so touch/callback re-entry cannot corrupt the outer call.
`PMCL_SetMoveVars()` is the boolean gate; no stale movevars are used after a
failed selection. It captures and retains positive CSQC contact IDs before
callbacks, restores scratch, then calls `SV_LinkEdict(entity, true)`. The
narrow `sv_phys.c` dispatcher revalidates retained CSQC entities and reuses
the existing private `SV_Impact` for each still-live solid contact. Negative
engine-network IDs remain collision-only and are never interpreted as QC
edicts. This preserves trigger-before-impact ordering without recursive
shared-scratch hazards.

The reused impact path does not enter co-op friendly-fire state: its existing
`SV_FriendlyFireServerValid` requires `qcvm == &sv.qcvm`. Shared-pickup and
telefrag helpers also reject CSQC entities because their server-client pointer
must equal the current edict. The former `world.c` recent-teleport exposure is
resolved by main's narrow `SV_IsActiveClientEdict` guard: it now requires the
server VM and the matching `svs.clients[slot].edict`, while retaining its
existing active/spawned/`FL_CLIENT` checks. No CSQC-specific world state was
added.

## Main-owned integration boundary

1. Integrated by main: `QCEXTGLOBAL_FLOAT(clientcommandframe)` and
   `servercommandframe` are published on every CSQC VM entry from canonical
   `cl.movemessages` and `cl.ackedmovemessages`; `CL_ClearState` owns their
   reset naturally. `CL_GetCSQCInputState(unsigned int, usercmd_t *)` is the
   sole history/pending owner: it tag-checks committed slots, rejects zero and
   stale/future/empty sequences, and builds the pending command with
   side-effect-free `CL_PrepareReplayPreview`. #345 does not duplicate policy.
2. The adapter's `SV_DispatchCSQCPMoveImpacts` is the sole added server-world
   surface. It remains next to `SV_Impact` in `sv_phys.c`, accepts only the
   adapter's retained pointer/number pairs, confirms the active CSQC VM,
   current edict bounds and identity, and dispatches still-live BBOX,
   SLIDEBOX, or BSP contacts through the existing implementation.
3. No new native-prediction owner policy is part of this API. The existing
   `CSQC_Input_Frame` unsent-preview suppression remains: it prevents
   unfiltered unsent buttons from replaying, without disabling ordinary
   committed native prediction. The committed journal and acknowledgement
   replay owner remains unchanged. These APIs alone do not claim complete mod
   prediction or renderer/`CSQC_UpdateView` coverage.

## Validation fixture

`tests/csqc_prediction_fixture.c` records numeric conversion guards without
execution. The deferred selected-suite integration cases cover helper-owned
zero/stale/future/empty history and pending preview, entity VM/range checks,
non-finite movement/bounds/timestep, failed movevar selection, linked CSQC
plus engine-net collision, and nested PMove restoration.

`tests/csqc_prediction_native_fixture.c` and
`tests/run_csqc_prediction_native.py` add the meaningful loaded-VM witness:
the generated CSQC calls builtin 345 for tagged history, rejection, and the
non-consuming pending preview; it calls 347 for movement, byte-for-byte PMove
and movevar restoration, a post-restore trigger that re-enters 347 on a
separate CSQC entity, and a retained CSQC-solid impact callback after the
trigger. It does not claim renderer or `CSQC_UpdateView` coverage.

No builds or tests run until all seven selected implementation slices are
complete, per user direction.


Final batch: numerical guards and the loaded-QC native runner pass. Builtin345
verifies tagged history, rejection and pending preview. Builtin347 verifies an
actual change from the initial player coordinate in a valid loaded-world
location, byte-for-byte scratch restoration, reentrant trigger callback and
retained solid impact. This qualifies the API, not a complete third-party
CSQC renderer or prediction policy.
