# Quad views for Frame and large maps: 2.0 design decision

Status: assessed on 2026-09-29; the user decided **not to pursue quad views in
the 2.0 migration**. Retain two-view multiview with eye-tracked FB/META density
maps where qualified, and KHR shading rate where FB/META cannot work. The user
will measure performance after implementation; no build, runtime or headset
test is part of this assessment.

## Behavior and evidence

The release priority is Steam Frame (streamed and native), Beyond 2e/Monado,
and large maps such as Mjolnir `mj4m1`. Eye tracking is optional; foveation off
and explicit fixed mode must still work without gaze. Desktop graphics, SSAO,
MSAA, HUD, weapons and crossplay should retain their existing behavior.

OpenXR 1.1's optional
[`PRIMARY_STEREO_WITH_FOVEATED_INSET` view configuration](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#view_configurations)
has two wide views and two foveated inset views. The compositor supplies the
inset FoVs and may move them per frame; the application renders all four. The
wide views still cover the inset region for blending. A runtime must advertise
the configuration through `xrEnumerateViewConfigurations` and usable sizes
through `xrEnumerateViewConfigurationViews`; OpenXR 1.1 support alone does not
imply this optional configuration. The older
[`XR_VARJO_quad_views` extension](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_VARJO_quad_views.html)
is another possible route, not a requirement for every 1.1 runtime.

The [Quad-Views-Foveated author's explanation](https://github.com/mbucchia/Quad-Views-Foveated/wiki/What-is-Quad-Views-rendering%3F)
shows examples where four lower-total-pixel views can improve quality and
performance, and warns of duplicate geometry/CPU work and screen-effect
compatibility. Its Quest Pro and Varjo Aero pixel tables are *pixel-count
examples*, not Frame or `mj4m1` frame-time measurements. Its API layer's stated
graphics support is D3D11, so it does not supply a Vulkan integration for this
port. [NVIDIA's VRS description](https://developer.nvidia.com/vrworks/graphics/variablerateshading)
confirms the alternative tradeoff: spatially vary fragment shading without
lowering visibility/raster resolution. This saves shader work but not the
visibility work that quad views can reduce. Its DX11 VRSS wrapper is not a
drop-in Vulkan solution.

Valve's [Steam Frame custom-engine guidance](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
recommends the FB/META foveation extensions and EXT eye gaze, without
documenting quad-view support. The [Khronos runtime inventory](https://github.khronos.org/OpenXR-Inventory/runtime_extension_support.html)
does not establish quad-view support for SteamVR or Monado; it is self-reported
and cannot rule out a core 1.1 view configuration. Frame streamed/native and
Beyond/Monado capability therefore remain unknown until enumeration on those
actual runtimes.

Current owners are concretely two-view: `vr_openxr.cpp` has `kViews = 2` for
swapchain, view location, image submission and session config; `r_passes.c`
uses Vulkan multiview `viewMask = 3`; `glquake.h` stores two eye matrices;
`r_world.c` unions two-eye PVS/frustum/backface results and shares world draw
recording. VR SSAO, hidden-area masks, particles, transparency, HUD and weapon
presentation also consume the two-view target. Replacing only the XR swapchain
would not implement correct four-view rendering or culling.

## Cost model and architecture choice

| Route | Reuse, expected win and added cost | Decision |
| --- | --- | --- |
| Existing two-view multiview plus FB/META density map, or KHR shading-rate fallback | Reuses the existing XR gaze/profile, passes, culling and single stereo world draw. Reduces fragment shading; keeps full raster/depth and current SSAO/composition semantics. | Primary route for Frame and `mj4m1`. Finish and qualify this path first. |
| OpenXR four-view wide/inset path, selected only when runtime advertises it | Can reduce *both* fragment and raster pixel work at very high resolution. Adds two projections, more scene visibility and geometry work, swapchain images, composition, effect handling and potentially more GPU/CPU work in big maps. | Excluded from 2.0. Do not duplicate the existing runtime state machine. |
| App-composited inset layers without runtime four-view configuration | Requires custom projection/compositing and gaze handling and duplicates OpenXR's view contract. | Reject for this migration. |

For an equal-quality comparison, count all four quad-view pixel areas against
the two full-resolution stereo areas; then account separately for world draw,
geometry, depth, shadows, transparency, SSAO, resolve/composition and XR
overhead. Pixel savings alone cannot show net frame-time savings. `mj4m1` may
be CPU/visibility/draw limited, where two extra views can regress performance;
that is an inference from this renderer's work topology, not a measured
benchmark. A high-resolution, raster-bound Frame workload might favor quad
views even if VRS works, but pursuing it would require a new user decision.

## Evidence needed if the user reopens this decision later

1. On Frame native and streamed runtimes, and any Beyond/Monado target claimed,
   enumerate a usable four-view configuration with per-view dimensions and a
   stable gaze-following inset. Do not infer support from OpenXR API version or
   extension inventory. Missing gaze continues in ordinary stereo; fixed
   foveation remains explicit only.
2. Compare two-view FB/META (or KHR on that GPU) with four views at comparable
   central angular resolution, image quality and MSAA/SSAO settings. On a
   representative `mj4m1` route and a more pixel-heavy scene, require a
   repeatable improvement in *total* CPU and GPU frame time without a worse
   latency or quality result. The user owns the later performance measurement;
   no numerical gain is claimed now.
3. If both gates pass, first implement a narrow optional view-config adapter
   in the existing `vr_openxr.cpp` owner, using runtime-supplied four view
   poses/FoVs/sizes. Extend the existing scene renderer's view count, per-view
   culling, pass and presentation ownership; preserve the two-view path as the
   capability fallback. Prove correct inset edge blending, both-eye PVS,
   hidden-area handling, depth, SSAO, MSAA, transparent effects and UI before
   enabling it by default. Reopen the architecture review if this duplicates
   the render graph or requires a second scene state machine.

The smallest four-view vertical proof would be one stock room with correct
four runtime-provided projections and composition while desktop and two-view
XR remain unchanged. No four-view implementation is planned for 2.0.
