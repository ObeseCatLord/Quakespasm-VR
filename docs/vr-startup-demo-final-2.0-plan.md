# F05 native VR startup demo boundary

2026-10-01. Actual private stock startup log reports Playing demo1.dem after
the requested map was entered during loading. Controlled camera observations
show cls.demoplayback and tracked_aim_ready false; V_UpdateTrackedAim deliberately
rejects demo playback. VR demos are explicitly excluded from the supported goal.
Automatically entering that excluded path with a normal VR launch is undesirable.

Existing owner: Host_Startdemos_f retains native desktop playlist/setup and its
cl_startdemos opt-out branch sets demonum=-1 and opens the menu. Reuse that branch
when V_TrackedSessionActive is true. That existing function asks GL_OpenXRFrame,
which reflects actual attached stereo state; a failed XR launch that falls back
to desktop therefore keeps native desktop behavior. No new launch/configuration
policy, saved-setting overwrite, demo implementation or renderer rewrite.

One conditional addition in host_cmd.c; review the source and rebuild host.
Qualify default stock XR startup's actual no-demo/menu state, then use native
command queue after startup to load e1m1 and run controlled six-axis GPU camera
proof. Desktop startdemos stays unchanged; observe its actual playback separately
without adding VR demo support. The earlier prepared camera/direct projection
fixtures already pass and do not depend on this startup owner.

Earlier foveation GPU phases remain actual rendering/transition evidence, but
their playback scene is not live e1m1 gameplay or a 6DoF certification. Correct
that receipt explicitly. Earlier private connected gameplay is a separate
natural signon/QC/input proof. Final affected Linux/ARM refresh remains under F10.
