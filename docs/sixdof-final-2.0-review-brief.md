# Bounded final camera/startup integration review

2026-10-01. Final checklist F04/F05; user asks whether previews mean a flat
screen instead of positional VR. Solo game fork, reuse vkQuake native render
and inherited aim owners. Do not reopen the frozen ten-owner feature inventory.

| Fact | Evidence / status |
| --- | --- |
| Production workspace | quakespasm-2.0, branch2.0 established once; main/reference read-only |
| Camera path | [verified: source] vr_openxr.cpp locate_frame uses head VIEW-space and two xrLocateViews poses at predicted time; end_frame submits projection views |
| Native 3D vertex path | [verified: source] world.vert/alias.vert retain model MVP; stereo.inc per-view correction uses gl_ViewIndex |
| Full pose mapping | [verified: source] gl_rmain.c R_PrepareStereoFrame XYZ center, full basis, eye offsets; view.c tracked aim and roomscale body owner |
| Prepared six-axis proof | [verified: executed] Luna/xhigh two fixtures exit0; main read complete new cases and independent expected bases. ±XYZ, yaw/pitch/roll, IPD and restore; direct projection adds composed pitch/cant |
| Actual GPU route | [verified: executed earlier] isolated simulated Monado25.1/RTX4090/native Vulkan/validation; native SSAO1/MSAA4. No headset/gaze/provider/performance claim |
| Automation correction | [verified: run log] stock quake.rc entered demo1 during +map loading; cls.demoplayback bypasses V_UpdateTrackedAim, tracked_aim_ready0. This is excluded unsupported VR demo playback, not evidence against gameplay 6DoF |
| Earlier foveation proof | [verified with limit] actual setter/map upload/MSAA/AO/phase output proof; playback scene is not live e1m1/gameplay6DoF. Receipt will correct that label |
| Small production change | [verified: source/build] Host_Startdemos_f reuses cl_startdemos opt-out/menu branch when V_TrackedSessionActive. Host DEBUG build exit0. Default XR menu and live controlled GPU proof still running |
| Runtime/device proof | [unverified/user-deferred] physical Beyond/Frame, real gaze/FB/META, hardware controllers |

Primary decision: does this existing incremental camera adapter deliver proper
6DoF world rendering, and is the one-condition unsupported startup-demo guard
the narrow correct boundary? Lean yes, pending actual live GPU results. Compare
source against native 3D projection/direct math, challenge misplaced rotation or
translation (especially body-owned roomscale), old-frame task observations,
reference/aim alignment and menu versus world geometry. No renderer rewrite is
justified by a flat compositor image. Do not re-review unrelated netcode/audio/
QC/avatars or add features. Final overall Astra signoff remains F10 later.

Read-only scope: gl_rmain.c R_PrepareStereoFrame and related reference/vector
helpers, view.c tracked aim/base/roomscale functions, vr_openxr.cpp location/
end_frame, Shaders/{world.vert,alias.vert,stereo.inc}, host_cmd.c Host_Startdemos_f,
two camera/math fixtures, tests/openxr-sixdof.gdb, this plan/brief and private
sixdof-current logs plus sixdof-gpu-current/result.json/run.log if available.
Private root /tmp/qsvr-final-qualification-thchgzi8. GPU recipe deliberately
injects consistent head/eye poses after location and mirrors submitted poses;
this is a controlled GPU consumer check, not runtime sensor validation.

Output <=1200words, verify before critique, prioritized actionable findings with
file/line evidence, labels for claims/limits; distinguish real production issues
from fixture deficiencies. Challenge deletion/simplification, avoid testing
framework expansion. Human decisions only if genuinely needed. No edits,
subdelegation, raw operational telemetry or goal-complete declaration.
