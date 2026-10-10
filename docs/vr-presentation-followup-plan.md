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

## Additional verified settings and enhanced-model scope

The completed settings trace found definition-time spark/beam options without
reload callbacks. Reuse the particle script's existing reload owner when those
preferences change. Describe `gl_farclip` as sky distance rather than creating
a new geometry culling plane. Foveation status should distinguish the selected
request from an inactive backend using the existing status/capability owner;
no foveation fallback or rendering policy is added.

Menu-exposed particle, sky and liquid preferences that lacked archive flags
will use the existing configuration persistence mechanism. Preserve defaults,
map-authored precedence and zero-valued liquid inheritance; no second settings
store or migration is needed.

The original neutral MD5 viewmodel transform applies a separate scale only to
official rerelease replacements: axe one third, double shotgun one, other
weapons one half. The current enhanced lookup correctly ignores classic authored
scale, but loses this source distinction. Applying one half to every MD5 would
change custom native models and is rejected.

Reuse `COM_VerifyRereleaseModelPack` once when mounting a candidate official
pack. Retain that immutable provenance on the pack and the selected model
companion; extend the existing VFS lookup with an optional source output rather
than duplicate its search policy. Reset model provenance with model memory.
A pure selected-geometry scale helper supplies the original scale factor to the
held alias matrix and the calibration inverse. Muzzle offsets remain independently
authored as in 1.0; preview meshes and desktop models retain native scale.
Pack provenance identifies the source; each selected weapon's actual mesh and
animation also require their length/CRC fingerprints before official scaling.
Check ordinary and fallback official packs, loose overrides, custom MD5 models,
classic/enhanced switches and calibration round trips after implementation.

## Final Astra review and bounded corrections

The affected hardware is Beyond with Index controllers. Local Astra reviewed
the integrated implementation at explicit `xhigh`; main verified the fallback
muzzle lever and the pack verifier's Ranger-only payload checks.

| Finding | Disposition and implementation plan |
| --- | --- |
| Grip/muzzle fallback exaggerates known axe angular travel | Adopt. Fill missing authored endpoints from the existing selected verified blade cache or prepared held-tool owner. Preserve authored endpoint precedence and unknown-model fallback. Rebaseline history when the endpoint source changes. |
| Official pack admission does not prove each weapon payload | Adopt. Qualify the actual selected mesh and animation bytes at load time using donor fingerprints before assigning official scaling. Retain the existing winning-VFS provenance and pure rendering query. |
| Official MD5 axe lacks ready-blade cache | Adopt. Cache only ready vertices 55/54 with exact mesh/animation and topology checks, using existing packed skinning math once during loading. Keep a separate selected-format record, clear it on reload/free, and require the effective ready pose. No per-frame CPU skinning or new combat implementation. |
| Sky fog archive preference is reset on map cleanup | Adopt a narrow correction. Reset only the existing runtime `skyfog` from the preference; preserve the cvar. `Sky_NewMap` already applies worldspawn overrides to the separate runtime value. |
| Increase VR shader light/shadow strength | Reject without evidence of attenuation. Keep native vkQuake gains and repair settings, prerequisite reporting and dynamic-light cleanup. |

End checks must now include real blade angular gestures, source transitions,
partially authored endpoints, altered equal-length weapon data and overridden
animations. Simulated rendering is software qualification; headset appearance,
comfort and performance remain user evaluation.

Both blocking final-review findings were implemented and re-reviewed by local
Astra at explicit `xhigh`. Classic caches require effective pose zero and are
selected independently of server contact classification. The source/format/
pose keys preserve ordinary prepare-to-merge identity and invalidate source
changes. Actual mesh and animation qualification precedes decoding/freeing;
success-only publication and a separate MD5 cache preserve reload/failure and
classic-companion behavior. No remaining source-review blocker was identified.

## Surface-bound VR pointer follow-up

The dot's positive depth bypasses collision, and the existing crosshair trace
checks only the world hull. The untextured blended draw also ignores scene
depth. Reuse `CL_TraceLine`, which already traces the world and moving/rotated
brush entities, inside the existing immutable crosshair preparation. Interpret
positive dot depth as a maximum trace range; zero retains the normal long ray.
Hide a dot with no surface hit instead of placing it on an arbitrary plane.
Apply vertical ray adjustments before tracing, never after the hit.

Reuse the existing depth-tested blended world-glyph pipeline with the persistent
white texture for the actual pointer. Keep the explicit calibration cue's
overlay policy and the two-hand aim owner. This avoids another trace solver,
pipeline family or eye-specific target calculation. End checks cover fixed-range
near obstacles, doors/rotated brushes, misses, two rays, depth-tested drawing and
calibration separation; desktop crosshairs remain native.

## Completed end verification

- Strict assertion-enabled Linux build passed after all implementation changes.
- Focused presentation checks passed: Index origin/velocity, coherent gestures,
  held calibration, wheel geometry/picking, dynamic-light cleanup, particle reload,
  menus, selected VFS provenance and actual official mesh/animation pairs.
  Geometry/provenance fixtures additionally passed ASan/UBSan.
- RTX 4090 Vulkan sampler readback confirmed nearest/linear magnification and
  desktop/stereo parity with anisotropy 2/4/8/16.
- Isolated native desktop and both simulated stereo-eye checks confirmed AO
  quality changes, dynamic-light toggling/restoration and map preference behavior
  with task rendering and GPU lightmaps enabled.
- Native Honey gameplay rejected small wrist wiggles and a held trigger, then
  issued one deliberate gesture attack through normal usercmd, loopback and QC;
  the disposable target took the normal 20 damage. Three native animation-frame
  callbacks were observed, not three attack intents. The actual decoded MD5 blade
  cache matched the packed shader calculation within 0.000002 model units.
- Surface-pointer fixtures passed world and moving/rotated brush hits, range,
  miss hiding, two-ray compaction and calibration separation. The final native
  Honey stereo run observed 62 normal pointer draws using the existing
  depth-tested world-glyph pipeline family and white texture.

These are software integration checks with isolated simulated controller input.
Physical headset appearance, comfort, eye tracking and performance remain user
validation. No physical headset or live co-op server was used for these checks.

## Release completion

Engine revision `d939ec2f4695c55b5f86cc4c07e0e668c7374e9f` passed native
Linux x64, Linux ARM64 and Windows Release builds and complete runtime inventory
checks. The automated workflow deployed its matching Linux runtime and published
all three packages to the 2.0 update channel and GitHub v2.0.0. Public artifact,
metadata and GitHub attachment hashes were verified. Physical headset evaluation
remains user validation. A separate publisher-only connection recovery correction
was tested and used without changing or rebuilding the qualified engine inputs.
