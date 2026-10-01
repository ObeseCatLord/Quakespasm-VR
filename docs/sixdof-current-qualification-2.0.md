# F04/F05 six-axis camera and native startup results

2026-10-01. Through production8071a46b. Main used actual source, reviewed
fixture implementation and verified local Astra/xhigh review.
This is a bounded camera result, not whole F04/F05/goal or headset acceptance.

The world is native 3D stereo: runtime head/view location, translated full-basis
native view matrix, model/world vertex projection and gl_ViewIndex eye correction,
then two XR projection views. HUD/menu geometry is a separate in-world panel.
Official [xrLocateViews](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrLocateViews.html)
provides predicted eye poses/projection data; the
[projection-view specification](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrCompositionLayerProjectionView.html)
defines the submitted pose/FOV and runtime remapping. Source owners are
vr_openxr.cpp locate_frame/end_frame, gl_rmain.c R_PrepareStereoFrame,
view.c tracked-view functions and Shaders/{world.vert,alias.vert,stereo.inc}.

Prepared actual-source camera fixture passes twelve signed cases: ±XYZ0.2m,
±yaw/pitch/roll15°, independent expected center/basis, consistent32mm-per-eye
IPD, native command/controller aim separation and restored base. Existing
roomscale/reference/floor/aim cases remain passing. Direct eye-space projection
fixture now includes pitch/composed rotation, rigid canted eyes, translation,
asymmetric FOV and reversed depth. Both README compile/run recipes exit0;
main reviewed full additions and the initial scalar effective Luna/xhigh setting.
Later effective-context verification found resuming that coding agent had changed
it to the default Sol/high. Main stopped it, reviewed all resumed fixture changes
and finished the final repeated-reference-flag case itself. Do not label those
resumed additions Luna/xhigh. Future coding tasks need a fresh explicit spawn.

Actual native Vulkan/isolated Monado GPU run also passes: default stock XR
startup menu/no-demo, then native command queue loads e1m1. Eight settled phases
baseline/right/left/up/forward/yaw/pitch/roll; protocol0, controller mode7 and
tracked aim ready. Native center/basis/eye origins match independent expected
rigid transforms, MSAA4/SSAO1 retained, foveation off. GPU renders actual acquired
images, native tasks remain enabled, no validation VUID/errors/sync hazards,
explicit startup/pass markers, JSON passed and GDB/process0 natural quit.

Main inspected baseline/right/pitch/roll native left-eye mirror images before
runtime remapping: near/far geometry shifts differently under lateral motion;
level geometry rotates under roll and changes perspective under pitch. These
are native eye image snapshots, not a geometry layer displayed on a flat panel.
Main also inspected baseline/right/forward/pitch/roll compositor captures.
Application-only injected rotations can be compensated when the compositor maps
submitted views to its unchanged simulated physical head; lens-mask movement
in that compositor preview alone is not world-camera evidence. This test
injects consistent head/eye poses after actual runtime location and matches
submitted projection poses. It does not establish actual sensor tracking.

The startup finding was real: stock quake.rc started demo1 during requested
map loading; demo playback deliberately bypasses tracked aim. Host_Startdemos_f
now reuses native menu/opt-out branch for an attached XR session. Default VR menu
passes. Fresh native desktop profile still plays demo1, renders12frames and
exits normally0 with clean validation. Desktop settings/policy are unchanged;
failed-XR fallback retains desktop selection by source, not separately executed.
[Before-code startup plan](vr-startup-demo-final-2.0-plan.md).

Earlier attempts retained independently: overly strict horizontal-eye assumption
(corrected to retain native eye-local offsets/cant); pre-task camera observation
(observer moved to GL_EndXRFrame after draw-done join); demo camera mismatch
(not gameplay), separate custom-profile startup timeout, startup cl_startdemos
attempt still entering demo, missing xwininfo capture tool (uses installed xprop
and exact inferior PID ownership instead). Failed attempts are not passes.
Only owned game/compositor windows captured; no user desktop/focus/key events.

Astra found an additional source-derived paused-private counterexample: latched
body ownership zeroed horizontal view movement while the prepared player base
was frozen. Before-code source fixture failed134 with fresh delta0 instead of
(3.937008,-5.249344,0). The narrow8071a46b repair retains two horizontal values
and validity for the last unpaused body-owned rendered head. Camera and canonical
head/body queries share fresh paused displacement; queries cannot advance the
normal rendered snapshot. Missing paused baseline seeds once; reference/session/
client-map/owner retirement clears it. Initial playspace/LOCAL reference remains.

Final actual-source camera fixture compiles/runs0. It also passes composed XYZ/
yaw/pitch/roll at game yaw37 against an independent quaternion oracle, repeated
paused canonical/camera agreement, unavailable-pose/base recovery, repeated
reference_changed queries, missing-reference/session/owner/client-reset baselines,
valid predicted poses without active tracking, resume and repause. Main reviewed
the complete additions and reran its final reference case.

Actual roomscale input accumulator under ASAN/UBSAN compiles/runs0: pending
pre-pause movement cleared; paused positions invalidated; first resumed sample
baseline0; next fresh step only(0.8,-0.5,0). No renderer writes to commands, and
this separate input consumer establishes the existing discard/rearm dependency.

Actual private GPU run also passes four20-frame phases at current8071a46b input:
natural private negotiation; ordinary input/command producer activates native
cl.cmd.vr_active from controlled neutral Index grips; baseline/paused horizontal/
paused-repeat/resumed. Renderer body/snapshot ownership is latched. Camera moves
from(480.03125,-351.96875) to(485.28058,-348.03174) while server body remains
(480,-352,88.03125); repeated paused pose does not accumulate. Last/pending
roomscale payloads0 throughout the sampled phases, MSAA4/SSAO1 retained, no
validation errors/VUID/sync hazards, JSON/pass markers and GDB/process0 normal quit.
Main inspected baseline/paused/resumed native eye mirror. No negotiation, body,
commands, active tag or runtime head-tracking flag was assigned by this probe.
Hand poses/action availability and head/eye poses are controlled, not hardware.

Resume explicitly restores collision-resolved body view, retiring paused visual
displacement. It can move the camera back when the physical head remains displaced;
seamless continuity across resume is not introduced by this focused repair.
Initial private probe omitted the referenced preset/used the wrong Index enum and
did not establish ownership. After that recipe correction it exposed an overstrict
new render-snapshot active-tracking gate: simulated head valid1/tracked0, native
VR command active1. The repair now uses existing renderer/motion valid-pose
eligibility; stricter wire/avatar pose gates remain. Initial missing predicate
declaration was corrected, final full host build0. Both failed attempts retained.
[Focused repair plan](paused-roomscale-camera-final-2.0-plan.md) and
[senior dispositions](sixdof-final-2.0-review.md). This closes these camera/pause
subsets, not all F04/F05 or final F10; no new feature inventory.

Private evidence root /tmp/qsvr-final-qualification-thchgzi8:
sixdof-current/{vr_stereo_camera.log,vr_openxr_math.log};
sixdof-gpu-current/{run.log,result.json,*-mirror.png,*.png};
desktop-startup-current/{probe.gdb,run.log};
private-pause-gpu-current/{probe.gdb,run.log,result.json,*-mirror.png};
sixdof-current/{paused-before-fix.log,camera-main-final-build.log,
camera-main-final-run.log,pause-input.log};
logs/{vr-startup-demo-host-build.log,paused-roomscale-host-build-reset.log}.
CPU fixture and native GPU producer limits
remain distinct. Final affected Linux/ARM package refresh belongs to F10;
real headset/controller/provider testing remains user-deferred.
