# Actual simulated OpenXR GPU qualification

2026-10-01. **PASS for the tested24-probe matrix.** Full host Meson build through
production `a1df3ffd`; subsequent changes through `48e026e0` affect packaging/
documentation only. GCC16.2.1, SDL3.4.16, Vulkan headers/loader1.4.357,
RTX4090 Vulkan1.4.351/driver615.71.09. Host Steam Audio disabled; this is not
full portable/audio artifact acceptance.

Installed Monado25.1 runs an isolated simulated device/service with private
runtime/config/data and XCB compositor. OpenXR manifest:
`/usr/share/openxr/1/openxr_monado.json`. Private validation layer1.4.357;
system layer/runtime settings and game assets were not modified.

Reproduction uses `tests/openxr-local-smoke.gdb`, host build
`/tmp/qsvr-final-host-build/vkquake`, and an isolated asset profile under
`/tmp/qsvr-final-qualification-thchgzi8/gpu/game` with read-only pak0 symlink:

```sh
qualification_root=/tmp/qsvr-final-qualification-thchgzi8
env XDG_RUNTIME_DIR="$qualification_root/gpu/runtime" \
 XDG_CONFIG_HOME="$qualification_root/gpu/config" \
 XDG_DATA_HOME="$qualification_root/gpu/data" \
 XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json \
 VK_LAYER_PATH="$qualification_root/gpu/layers-current" SDL_VIDEODRIVER=x11 \
 XR_SMOKE_CAPTURE="$qualification_root/gpu/stereo.png" \
 timeout --signal=TERM 110s gdb --return-child-result -batch \
 -x tests/openxr-local-smoke.gdb --args /tmp/qsvr-final-host-build/vkquake \
 -validation 2 -basedir "$qualification_root/gpu/game" \
 -window -width 640 -height 480 -nosound -nojoy -openxr +map start
```

Local final log: `gpu/logs/xr-gpu-smoke-final.log` under that root. Markers
`XR_SMOKE_probe=0` through23 occur exactly once; zero validation errors, zero
hazard-detected messages, zero XR_SMOKE_FAILED markers. Inferior exited normally
and the clean command returned0. Native tasks and GPU lightmaps enabled;
stereo SSAO quality1 active; two-eye session896x1007 per eye.

Coverage: OIT modes0/1/2, MSAA1/4, indirect rendering0/1, palette then native
color, pause/resize/scale/floor/chase/head and mouse aiming, authoritative firing
alignment and normal shutdown. Eight initial/resize/scale/floor/chase/aim/pause
captures were generated. Main inspected the initial two-eye capture and verified
rendered start-map world/HUD in both eyes. No claim that all captures were
visually inspected or every effect/content case was exercised.

Limits: actual Monado/Vulkan rendering and production full-engine owners,
simulated device/input. Runtime exposes neither required gaze nor FB/META
foveation for this run, so rendering used the full-quality route. This does not
qualify actual eye tracking, runtime/GPU foveation, headset presentation,
controller gameplay, full content, audio, native Frame or performance. Desktop
hazards/abort remain separate failures. All eight full qualification groups
remain open.

## Current pass/shutdown repair refresh

Production55eaa33b, preliminary host rebuilt including c8de89f6 guarded physics
initialization, d7ef88bf reuse of native render-resource shutdown and55eaa33b
stable unused-attachment compaction. Main reviewed both complete source patches;
no global diagnostic barrier remains. Existing logical first-use/stencil policies,
special rate attachment retention and physical framebuffer/clear remapping checked.

The same actual Monado/Vulkan24-probe matrix passed again: probes0–23 exactly
once, no validation errors/hazards/failed markers, command exit0 and normal inferior
exit. Private log: gpu/logs/xr-pass-shutdown-final.log; capture stem
gpu/stereo-pass-shutdown. Main inspected the new initial two-eye world/HUD image.
Same hardware/runtime/audio-disabled/absent-gaze-and-foveation limits as above.

Native desktop also reached signon4/twelve frames with no validation errors or
hazards and normal exit0, without debugger-injected cleanup/barriers. Separate
native screenshot run passed and wrote the rendered start-map image through the
engine screenshot/readback owner; main inspected it. Logs:
desktop-pass-shutdown-final.log and desktop-native-screenshot-final.log. Earlier
window-name capture harness failed/timed out; it supplies no product failure or
acceptance. Full desktop campaigns/content/lifecycle qualification remains open.

A separate15-probe native desktop run exercised OIT0/1/2, MSAA1/4, indirect0/1,
palette/native color, AO0/1,800x600 resize and explicit400x300 render/upscale target
with an actual dimension assertion. No validation errors/hazards, command exit0,
normal inferior exit; desktop-matrix-upscale-final.log. An earlier harness attempt
referenced nonexistent r_scale and an intermediate run did not prove upscale;
neither substitutes for this corrected run. These bounded cases resolve the
previously reproduced baseline desktop sync/quit symptoms in the tested settings,
not all eight final groups. Subsequent92bc7775 changes only model face admission;
these rendering results retain their exact production snapshot.
