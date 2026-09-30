# Optional gaze source and official contract checkpoint

2026-09-30. Current source inspection and fresh official documentation lookup,
not runtime/headset qualification. Keep the existing backend/policy and two
stereo views; no new provider, headset-name gate or quad-view path.

The official [Khronos eye-gaze extension source](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/ext/ext_eye_gaze_interaction.adoc)
requires gaze position and orientation tracking flags to move together. A
nominal gaze pose carries tracking; degraded gaze clears it. Therefore the
current paired tracked-bit requirement is appropriate; loosening it to accept
an untracked origin is not a demonstrated portability repair. The same source
defines sample time as the time the pose represents, which can be predicted or
interpolated, and uses zero when unavailable. Keep the existing expressed-time
interpretation rather than treating this as a sensor acquisition timestamp.

Current production owners inspected:

- `discover_runtime` separately admits the EXT gaze system property and META
  runtime eye-foveation property. META does not require the EXT action to exist.
- `create_gaze_actions/disable_gaze_actions` own the separate optional action
  set and space. Failed optional creation disables that path while retaining
  ordinary head/controller actions.
- `GL_BeginRendering/VRXR_SetGazeEnabled` apply the existing menu toggle and
  selected mode before action synchronization. Disabled/unsupported gaze is
  absent from the synchronized action sets.
- `sample_gaze` requires focus, active action, valid/tracked pose and finite
  normalized orientation/origin before publishing the ray. The shared policy
  owns freshness/stability; eye loss selects full rate, never fixed fallback.

No new production change is required by this reviewed flag/timestamp boundary.
XR-004 remains source-integrated with actual Linux/ARM toggle/action/session/
freshness/fallback software qualification pending. This checkpoint does not
qualify Beyond/Monado provider setup or Steam Frame device support, which remain
user follow-up. It also does not close the entire foveation/rendering inventory.
No builds/tests/probes were run; final software qualification follows all
implementation.
