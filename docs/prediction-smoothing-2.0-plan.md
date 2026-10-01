# C07: native ACK smoothing with coherent VR draw presentation

2026-10-01. Before-code plan after the local Astra xhigh review of the
[verified brief](prediction-smoothing-2.0-brief.md) and its focused draw-boundary
follow-up. Effective gpt-6-astra/xhigh was independently verified by main from
the matching turn-context metadata; the reviewer could not inspect its own
routing. No tests/builds/probes until all implementation is finished.

## Behavior and references

Restore the inherited optional presentation correction policy using the current
native QSS-M replay: defaultoff, duration0.10s, minimum0.125 and maximum4 units,
linear decay. Actual tracked VR caps duration at0.060s and error at1 unit.
Disabled, absent evidence, large errors and discontinuities snap immediately.
No quarantine, voting, old prediction solver, authority protocol or replacement
movement layer. Authoritative origins, commands, velocities, QC/collision poses,
gameplay muzzle offsets and transmitted tracking remain unsmoothed.

Primary cl_main.c1595–1734 supplies policy/decay and1433 supplies dynamic-contact
eligibility. Current CL_ComputeReplayPlayerMovement supplies native admission,
coherent private snapshot/ACK, accepted authority/permission/epochs, public
PREDINFO policy, exact command journal and solver. The render extension keeps
camera, local held weapons and physical HUD coherent when the user opts in.

## Senior dispositions and main source spot-checks

| Recommendation | Disposition |
| --- | --- |
| Carry exact ACK evidence in replay result, commit only at complete success | Adopted. Capture before ring writes, after native guards and finite accepted baseline. Preview failure still invalidates the provisional sole history. |
| One64-slot origin/sequence/eligibility sample ring | Adopted. Record finite completed committed commands only; exclude preview/shadow/baseline. Missing/overwritten evidence is not another sequence or zero. |
| Separate consumed ACK, active decay and history | Adopted. Every successful new ACK evaluation is consumed even if smoothing is refused. Expiry clears only active correction; no duplicate-ACK rearm or residual accumulation. |
| Existing resets are not sufficient; ordinary invalidation is not semantic teleport | Adopted. CL_InvalidateMoveSnapshot occurs during normal packets; do not destroy history there. Use a presentation context signature and semantic reset/replay/relink/camera exits instead. |
| Camera-only creates held/HUD mismatch | Adapted. User's no-regression intent selects coherent draw-only translation; no question to accept drift is needed. Reopen the estimate to include renderer-only held/HUD/beam boundaries. |
| Shared matrix and pose APIs also feed gameplay | Adopted. Main verified view.c1109/1161/1224 and existing input matrix API. Native R_AliasModelMatrix/R_HeldMeleeMatrix and all body/world-pose helpers remain unchanged. |
| Preserve entity pointer identity and held-melee dispatch | Adopted. Draw wrapper uses original pointers, native held recipe versus ordinary matrix dispatch, then local world translation and original winding. No entity clones. |
| Pause can retain an already applied camera offset | Adopted. Undo prior applied offset before any cached-base reuse/rebuild. Capture a new immutable frame offset once; decay is never recomputed by workers. Native reset hook is V_ResetTrackedAim, not an invented V_ResetCamera. |
| Keep private dynamic-contact exclusion | Adopted. Capture bounded ground/touch physent info!=0 immediately after each committed simulation; native dynamic IDs may be negative. Only smoothing eligibility changes. |
| Include mode2 beam near endpoint | Adopted. Offset draw-local beam start; authoritative trace and impact/reticle remain world anchored. |

Main verified the source hooks above plus native draw_done ordering, exact
QSS-M baseline/preview loops, primary defaults and death/contact policy. This
source review does not certify execution or VR comfort/performance.

## Phase1: native sample/error and immutable view offset

Write set: Quake/cl_main.c, client.h, view.c, view.h. Reuse the existing client
presentation error fields; add only necessary64-slot sequence/origin/eligibility
samples and one presentation context identity. Signature includes world/view
owner, protocol/dialect, movement mode, accepted private authority/permission/
epochs and camera/actual tracked mode. History is observational render state,
never an admission/permission owner. Disabled/nonfinite settings do not incur
simulation changes or sample writes.

After native guards and baseline selection, capture prospective reconciliation
evidence before any ring writes. Store committed finite origins with exact wire
command sequence and eligible contact bit. Shadow/preview never write it. Full
live success commits a new ACK's correction; failure invalidates provisional
history without changing native replay success/failure semantics. Numeric
smoothing failure disables only smoothing. Preserve public lost-history resume
and private complete-history requirement exactly.

Keep existing semantic reset calls and handle skipped relink preparation, live
failure, actual teleport/forcelink, no prediction, death, pause, demo/intermission/
chase/camera changes, time and ACK reversal. Do not classify ordinary UF_RESET
or normal snapshot receipt invalidation as teleport. Separate active-error expiry
from all-history reset.

V_SetupFrame removes its previously applied offset before deciding whether to
reuse or rebuild the camera. After native view/viewmodel preparation, evaluate
one eligible correction and apply it once to r_refdef.vieworg, publishing the
actual applied vector through a pure V_GetPredictionViewOffset getter. On
V_ResetTrackedAim, undo or replace the affected base before clearing offset
bookkeeping; metadata/history reset must not lose the ability to remove an
already applied offset. Do not mutate cl.viewent or shared tracked-pose helpers.
Frame/relink validity must not leave a correction active on an interpolated
camera after prediction stops.

## Phase2: renderer-only coherent VR consumers

After phase1 source review, write set expands to Quake/r_alias.c, glquake.h,
gl_rmain.c and the non-controller HUD target in gl_screen.c. This stage must
wait until the active C13 worker relinquishes its render files. Phase1 alone
does not close C07.

Introduce R_AliasDrawModelMatrix only at the render boundary. Dispatch held
melee through R_HeldMeleeMatrix and other entities through R_AliasModelMatrix;
after native success, translate local matrix[12..14] using the immutable applied
offset only for existing VR viewmodel identities (dominant, akimbo halves,
held-melee; includes head-aimed VR). Preserve winding/finiteness. Route ordinary
draw/show-tris and any eligible local overlay/skeleton/foreground/bounds
consumer consistently, retaining their actual native dispatch. Wheel previews,
avatar props/muzzles, world/avatar TLAS and all view.c/input matrices stay native.
Do not rely on a guessed foveation-bounds caller: inspect the actual consumer.

Controller HUD/rays already inherit corrected r_refdef; do not add again. Add
the captured offset to the non-controller HUD target only. Mode2 beam rendering
uses a corrected local near start while retaining authoritative trace/impact;
world-hit reticles remain fixed. Preparation helpers used by input/physics must
not receive a translated draw origin.

Expected phase1 adapter120–200 changed lines; phase2 another70–120. Scope
reopened explicitly for coherent VR output. Reopen again if a new authority,
protocol, simulation owner or broad rendering layer appears, or combined work
materially exceeds320 lines. No new metrics or perf measurements required.

## Final acceptance after all implementation

Show enabled/disabled/default-off corrections using exact ACK identities,
duplicate ACK without rearm, ring overwrite/loss and finite bounded decay.
Cover private authority/permission/epoch changes, public truncated history,
skipped/failed replay including preview failure, dynamic contacts, reset,
teleport/death/respawn/pause/demo/intermission/chase/mode changes and teardown.
NaN/infinite settings/origins, time reversal and large errors must snap without
altering native movement or networking.

Qualify actual desktop/native camera behavior and two-eye VR output: no cached
offset persistence/accumulation; camera, dominant/offhand/held draw, diagnostic
tris, controller/head HUD and beam near endpoint agree. Compare command bytes,
QC/collision input, gameplay muzzle/body offsets and authoritative motion with
smoothing disabled/enabled. Phase1 helpers/counters or packet evidence alone
cannot close full C07; rendered software qualification remains mandatory.
