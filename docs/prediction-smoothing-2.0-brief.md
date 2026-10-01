# C07 presentation smoothing: verified decision brief

2026-10-01. Solo project; narrow native replay adapter. No tests or builds now.
Local Astra xhigh review requested. Main keeps authority/architecture decisions;
later Luna coding will use a committed before-code plan.

## Verified current and reference behavior

All sources below were read directly; no executable behavior is claimed.

| Fact | Evidence |
| --- | --- |
| Writable checkout | Only /home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0, known2.0. Other checkouts/assets readonly; no branch checks/switches. User-dirty docs/migration-2.0.md untouched. |
| Behavioral reference | Primary quakespasm-openvr51b452c018273647dcf94f4628a370267ff8fa91 cl_main.c1595–1734 and view.c836: exact ACK predicted-origin reconciliation, defaultoff/time.10/min.125/max4, VR max1/time.060, linear camera-only decay. Its quarantine/error policy and legacy movement wrapper are not migration targets. |
| Native replay | Current cl_main.c1750 CL_ComputeReplayPlayerMovement: native/publicPREDINFO or coherent private owner/ACK snapshot, permissions/authority/epochs, command journal sequence checks; existing QSS-M movement/QC solver; committed commands then disposable unsent preview. |
| Native history | client.h movecmds64 and MOVECMDS_MASK; replay starts at ACK+1 (minimum2), excludes ACK itself. Public missed history resumes at oldest retained command; private requires complete history. |
| Baseline | Private uses ent->netstate.origin; public uses msg_origins[0]. Both are checked finite. Shadow trace is observational and saves/restores pmove/movevars. No preview/shadow sample is a completed wire command. |
| Presentation fields | client.h prediction_error/time/sequence exist; cl_main.c275 reset clears them. No producer/consumer exists. cl_parse resets on semantic teleport, pause and invalidation; CL_ClearState zeroes client. |
| Current publication | CL_ReplayPlayerMovement returns result into CL_PrepareRelinkViewPose.frame.vieworigin, then relinking publishes this into the view entity's presentation origin; netstate remains authoritative. Attachment code also uses that unsmoothed relink view pose. |
| View consumer seam | V_CalcRefdef copies ent->origin, publishes base player view/height/angles, then adds viewheight/bob before bias. R_PrepareStereoFrame builds eyes and controller mapping from that base. Existing primary smoothing adds only to r_refdef.vieworg. |
| No predicted origin samples | Current client owns no end-of-committed-command predicted origin journal. Existing propagation stores public waterjump timer only. |

## Minimal adapter lean

Copy the primary bounded smoothing policy and linear decay, with finite numeric
guards and modern private authority/coherence/reset gates. Do not copy its old
prediction solver, quarantine, large-error voting or metrics. Default disabled.
Add only a64-slot presentation origin sample alongside the existing command
journal, exact sequence-tagged, tied to view owner/protocol and private epochs.
Record after each successfully replayed committed command when smoothing is
enabled; never preview/shadow. Native journal, replay result and authority do
not change.

Compare the old exact ACK sample against the accepted baseline before new replay
overwrites any ring slot. Publish a correction only if the full native replay
pass succeeds, once per new ACK; missing/overwritten samples cannot be treated
as zero or reacquired from a later command. Clear presentation/history on native
discontinuity/failure/disable/context changes. Separate clearing a decayed error
from invalidating sample history, so normal decay cannot erase future ACK proof.
Time reversal, nonfinite cvars/vectors, too-large correction and teleports snap.

Apply only at view preparation after native movement completes; keep entity,
netstate, command journal, QC motion, velocity, collision, networking and tracked
input authoring unsmoothed. Use actual tracked rendering for VR comfort caps.
No prediction permission is acquired through this option.

Alternative: compare previous final rendered prediction at current latest
sequence against a new replay result, including a pending preview. Rejected:
sequence/time mismatch produces camera error from ordinary movement and reruns
stateful input; it is not the inherited ACK reconciliation contract.
Alternative: port the primary full prediction subsystem. Rejected: already
working modern native QSS-M owners are reusable, no demonstrated incompatibility.

## Decisions to verify and critique

1. Exact sample/ACK producer and success boundary: should the existing replay
   result carry a prospective correction for later publication, or should a
   narrow helper capture before compute and commit after success? Lean toward
   result-carried evidence after native guards/baseline checks to avoid duplicate
   admission rules. Both must protect ring wrap and duplicate ACKs.
2. Context/reset identity: use current native reset calls and one presentation
   owner/protocol/epoch signature, no parallel authority state machine. Determine
   all existing continuity exits needing reset or producer refusal.
3. Camera-only VR application: verify how stereo eyes/hand presentation derive
   from r_refdef versus entity origin. Decide the smallest coherent presentation
   boundary so a bounded camera correction does not alter gameplay pose authoring
   or make held weapons/world HUD float relative to the user.
4. Reuse actual native/public lost-history policy: if no exact ACK sample, do not
   smooth; never change replay availability just to produce a smoothing sample.

Expected source scope: cl_main.c/client.h/view.c, potentially existing reset
owner cl_parse.c only if a demonstrated missing reset requires it. Approximately
120–200 lines. Reopen if another protocol/solver/state-machine is needed.
Final checks will cover enabled/disabled correction, exact ACK+sample identities,
duplicate/lost/overwritten journal, private owner/epoch/permission changes,
teleport/death/respawn/pause/no-prediction/demo/camera changes, NaN/time reversal,
VR caps and unsmoothed authoritative motion. No performance measurement required.

Read-only review; C13 Luna owns avatar/render-model files, not this slice.
No agents/tests/builds/probes/edits. Verify first and return <=1000words
prioritized critique with file/symbol evidence and recommendations. No full
network redesign, unrelated QC, foveation/graphics, packaging or Windows review.
