# VR presentation follow-up

## Scope and references

Restore texture-filter selection, default held weapon placement, wheel picking
and action labels, and deliberate melee gestures using the inherited 1.0 source
as the behavioral reference. Keep the vkQuake renderer, shared graphics settings,
desktop presentation, authored weapon profiles and existing OpenXR tracking.
Audit stereo graphics settings before changing lighting aesthetics.

The read-only OpenVR reference is revision `bd923e92`. The affected session used
Honey co-op with Index controllers. Physical headset evaluation remains separate
from the end-of-implementation software checks.

## Verified differences and minimal implementation

| Area | Evidence | Plan |
| --- | --- | --- |
| Filtering | `TexMgr_SetFilterModes` overrides `vid_filter` with a linear sampler only in stereo when anisotropy is enabled. | Remove that override. Preserve nearest/linear texture flags, native anisotropy levels and descriptor refresh callbacks. Extend the existing Vulkan readback fixture across both filter modes and stereo/desktop. |
| Wheel centering | The new preparation subtracts only the model Z midpoint with unselected scale. The donor rotates and subtracts XYZ midpoint at the decoded draw scale. | Reuse the donor centering calculation inside the existing prepared-model owner. |
| Wheel picking | New playspace picking intersects enlarged model bounds; donor compares projected slot-center distance. | Restore slot-center picking, keeping current eligibility, visibility and release checks. Desktop picking stays unchanged. |
| Wheel actions | New playspace labels inherit resolution-dependent desktop pixel sizing. Donor uses 1.44-unit glyphs, 3-unit row pitch and 6-unit ring clearance. | Convert donor world dimensions through the existing panel transform, retaining depth-tested glyph rendering and matching hit rectangles. |
| Controller pose | Donor consumes raw tracked-device pose; OpenXR consumes grip pose. Valve's Index grip component translates `(0,-0.015,0.13)` metres and rotates `(15.392,+/-2.071,-/+0.303)` degrees. Monado's `vive_poses.c` implements the same transform. | Review a narrow Index pose adapter before implementation. Do not compensate by rewriting every weapon offset or altering other controller profiles. Preserve raw grip tracking for consumers that require it. |
| Melee | Sensitivity/rearming and donor gesture semantics are under comparison. | Resolve demonstrated differences before changing defaults. Preserve trigger suppression and gesture animation policy; reject wiggles and stale tracking. |
| Graphics | Shared stereo effect paths exist; weaker appearance alone does not establish a routing defect. | Trace settings to resources/passes/shaders, fix verified defects, then qualify meaningful settings in both eyes. Preserve desktop AO and OpenXR-owned render resolution. |

## Design boundaries

An adapter and corrections to existing preparation paths are smaller than a new
renderer, wheel state machine or per-mod calibration set. The current Vulkan
alias/glyph consumers, catalog, calibration schema, contact/gesture owners and
graphics callbacks remain reusable. No new shadow renderer or quad views.

Official pose reference: [Valve render-model reference](https://github.com/ValveSoftware/openvr/wiki/Render-Model-Reference).
Anisotropic filtering can vary by implementation, especially with nearest
sampling; retain the same requested sampler in desktop and stereo rather than
forcing linear sampling in VR ([Vulkan sampling specification](https://docs.vulkan.org/spec/latest/chapters/textures.html)).

## End checks

After implementation, use existing native fixtures and a coordinated engine
build. Cover nearest/linear with anisotropy off/on and descriptor transitions;
rotated selected-wheel centering and neighboring-slot selection; action text/hit
geometry; Index pose round trips for both hands; deliberate swing, wiggle,
rearm and pose-loss cases. Check applicable graphics options on desktop and
simulated stereo without touching GPU drivers or user profiles. Build/package
all release targets once at the end through the release coordinator.

## Astra senior review disposition

Local Astra reviewed the verified plan at explicit `xhigh`, read-only. Main
spot-checked the Index component/Euler convention, donor rearming guard and
CPU lightstyle behavior before adopting these recommendations.

| Recommendation | Disposition |
| --- | --- |
| One stateless compatibility device accessor, without mutating the frame | Adopt. Index grip is transformed back to the donor controller origin; other profiles pass through. |
| Correct origin velocity by angular velocity cross the translation lever | Adopt. Missing required angular velocity invalidates derived velocity, while a valid pose remains usable for rendering. |
| Preserve raw grip positions and orientations for wrists/FBT | Adopt. Explicit raw-grip wrappers share existing body-offset math; HMD/tracker/action records remain untouched. |
| Use compatibility poses for aim, held models, wheel anchors/rays and contact endpoints | Adopt. Keep one pure transform owner and existing presentation/input lifecycle resets. |
| Fixed-reference gesture travel and coherent same-endpoint effort | Adopt. Head movement must not alter the displacement witness; unfinished reversals reset rather than accumulate wiggle distance. |
| Rearming samples cannot earn a strike | Adopt from the donor server's existing recovery guard. Preserve native QC combat ownership and one synthetic gesture intent. |
| Default gesture effort needs tuning separate from correctness | Adapt to the user's explicit sensitivity reduction: implicit profiles request 1.5 m/s and 10 cm travel; authored slow gesture settings retain their intent. Headset comfort remains user evaluation. |
| CPU dynamic-light cleanup must preserve frozen baked styles | Adopt. Rebuild formerly dynamically lit surfaces with their cached styles while off, avoiding selective changes to baked brightness. |
| Strengthen VR shaders without evidence of attenuation | Reject for this correction. Preserve vkQuake strengths; repair controls and explain inactive shadow prerequisites before introducing a separate lighting policy. |
| Restore multiplayer offsets or add guessed melee cooldown queues | Reject. One offset set is intentional; original QC owns weapon cooldowns. |

The review caught two integration risks beyond the initial plan: shifting a
controller origin also shifts its linear velocity, and CPU-lightmap cleanup
would otherwise use newly averaged lightstyles only on formerly lit surfaces.
