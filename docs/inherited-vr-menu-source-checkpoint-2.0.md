# Current inherited VR menu source checkpoint

2026-09-30. Primary `51b452c0` has45 option enum members before MAX in
`Quake/vr_menu.h`. This bounded source comparison uses the actual draw/value/key
handlers in `vr_menu.c`, rather than treating every old cvar name as a required
second setting. Native checkpoint `6c87b1ea`; final software behavior remains
unqualified. Current user scope supersedes historical controls.

| Primary controls | Current native owner / disposition |
| --- | --- |
| Enabled | Primary enabled key case is commented out at653–658. Native explicit `vr_enable`/OpenXR lifecycle remains the runtime action; no missing working primary toggle established. |
| Left handed, VRIK, aim mode/deadzone, world scale, movement direction, floor, snap/180/smooth turn | Native Gameplay Setup exposes the existing settings through finite/bounded native edits. World scale does not admit primary's unusable zero. This is menu exposure evidence, not complete aiming/locomotion/rig behavior. |
| Gorilla, instant stop | User-deferred. Existing controls/owners retained; no new integration/qualification requirement. |
| Movement speed | Existing Joystick Tuning page, alongside native deadzone/exponent/truncation/yaw/menu deadzone controls. No second stick processing owner. |
| Crosshair mode/depth/size/alpha/Y, hidden-area mask, HUD/menu scale, wheel mode, haptics | Native main VR Options exposes these settings. Menu maximum restored from0.30 to primary0.60 in the one-line repair. Primary crosshair menu labels depth as units, but actual consumer multiplies by meters_to_units at `vr.c:10835`; native metre display/finer edit is retained. Separate haptics/input/panel/render reviews remain the behavior evidence. |
| MSAA | Native Graphics Options `vid_fsaa`/sample mode uses the existing Vulkan targets and shared desktop/VR policy. Do not reproduce primary's OpenGL GL_MAX_SAMPLES query or a separate VR AA owner. |
| Gun angle, model pitch/scale/Y | [Four-control Weapon Setup](vr-weapon-options-2.0-plan.md) now source-integrated through already registered cvars; exact inherited bounds/steps and native pointer/list routing. Main complete-diff review accepted. |
| Gun Model Offsets | [Named classic preset adapter](weapon-preset-adapter-2.0-plan.md) source-integrated, separate from native MD5/MD3 geometry preference. Existing Weapon Setup exposes five choices; contextual/reload/live/seed behavior reuses the native calibration owner. Main complete-diff and requested-Astra source reviews accepted after limiting live BlockQuake to its eight-row batch. Final software qualification pending. |
| Projectile Spawn Z | At the current primary pin, its only value read is conditional solo crosshair-source compensation, not actual shot origin. Retain native calibrated physical-muzzle crosshair and shared per-weapon QC source correction. No global24-unit projectile control or pointer/shot divergence to copy. This is an intentional functional improvement, not identical legacy display/setting parity. |
| Immersive melee, weapon collision | Existing Gameplay Setup controls; gesture-only/ready-pose/shared-profile decision is authoritative, physical-contact expansion deferred. Menu exposure does not certify damage/input behavior. |
| FBT enabled/hip/feet/profile/calibrate/cancel/rescan | Native FBT Setup reuses status, runtime roles, profile selection and transaction owners, with additional explicit capture/accept/save/reset. OpenXR dirty discovery/persistent paths replaces OpenVR serial-cache rescan; current command checkpoint records the source disposition. No second tracker cache. User live tracking qualification is separate. |
| Impulse9, God, Noclip, Fly | [Four gameplay actions](vr-gameplay-actions-2.0-plan.md) source-integrated under Gameplay Setup, using fixed native command queue strings. Native server/QuakeC policy and refusal remain; no invented toggle state. Main exact dispatch/array/page-geometry review accepted. |

Native eye/foveation toggles, mirror selection, menu follow, default microphone
and voice page remain additions. Eye data is optional; invalid/unavailable gaze
restores full-rate rendering and never fixed foveation. No quad views.

This closes neither every cvar/command nor inherited behavioral coverage.
It bounds actual primary menu exposure: classic presets now have a reviewed
source adapter, while other menu rows have source-integrated owners or
explicit dispositions. Startup/saved config precedence and exact consumers
need their own evidence, not a checkbox count. Source-only reads and scoped
whitespace checks; no builds/tests/probes/fixtures/benchmarks. Consolidated
Linux/ARM software checks follow full implementation; user headset/gaze and
performance trials remain outside the goal.
