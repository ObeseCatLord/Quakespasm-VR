# F05 six-axis world-camera qualification

2026-10-01. User asks whether flat compositor previews indicate a tilted screen
instead of required positional VR. This attaches to existing F04/F05 camera
acceptance; no new renderer architecture or product feature.

Verified source: vr_openxr.cpp locates the VIEW head space and both runtime
eye poses at the same predicted display time; requires valid orientation and
position. End-frame submits two XrCompositionLayerProjectionView entries with
their poses/FOV, not a scene quad layer. gl_rmain.c R_PrepareStereoFrame maps
XYZ translation, full head orientation and relative IPD to native world-camera
origins/basis. Body-owned collision-resolved roomscale removes duplicate
horizontal displacement. Shaders/world.vert and alias.vert retain native model
MVPs and apply per-eye projection correction using gl_ViewIndex. HUD/menu panels
and flat compositor captures are distinct from level geometry.

Smallest proof: reuse actual source camera fixture, prepared consistent rigid
head/eye poses for each positive/negative XYZ and yaw/pitch/roll axis. Independently
expected center/basis/IPD, restoration and controller-aim separation; extend
existing direct eye-space projection oracle with pitch/composed rotation. No
new camera implementation, runtime or duplicated tracking state.

Additional native-GPU bounded proof, if feasible: existing isolated Monado/host
DEBUG build. Inject controlled consistent head/eye poses after successful
runtime location and before clip preparation; mirror matching poses in submitted
projection views. Keep acquired runtime images/native render owners and actual
SSAO/MSAA. Compare native rendered-camera axes and origins with expected rigid
transforms, capture stationary near/far scene at baseline and lateral displacement
to inspect parallax, and inspect yaw/pitch/roll output. Require normal exit and
clean validation. Clearly label injected tracking, not real runtime sensor or
headset/roomscale-gameplay proof. If the boundary is unsafe or automation fails,
report that failure rather than weakening assertions or changing production.

Main owns plan, GPU recipe/receipt and integration. Luna owns only the two
existing fixture sources. Current source through4ef67790. Hardware/controller/
provider usability remains user-deferred; additional F04/F05 boundaries stay open.
