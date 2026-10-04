# Graphics menu expansion (2026-10-04)

## Outcome and boundaries

Expose the shipped vkQuake visual features through a readable graphics menu,
including the new optional clustered lighting and surface dithering. Preserve
renderer defaults, desktop behavior, and the shared desktop/VR SSAO quality
setting. Keep OpenXR eye resolution runtime-controlled. Sky rooms and developer
render diagnostics are excluded. This is menu work, not a renderer replacement.

## Verified baseline

| Fact | Evidence in current 2.0 tree |
|---|---|
| Two flat graphics pages; fixed eight-pixel rows, footer at 184 | `Quake/menu.c`, `M_GraphicsOptions_*` |
| AO and shadow rows disappear when unavailable | `M_GraphicsOptions_OptionVisible` |
| MSAA menu cycles 0/2/4/8/16 without a capability filter | `M_GraphicsOptions_ChooseNextAASamples` |
| Renderer selects supported MSAA and excludes Intel 16x | `Quake/gl_vidsdl.c`, `GL_SelectNativeSampleCount` |
| Anisotropy uses actual device support and limit | `R_AnisotropyLevel`, existing menu adjustment |
| AO already has off/low/medium/high, radius and strength | `Quake/r_ssao.c`, `r_ssao*` |
| VR AO evaluation can be half or full resolution | `r_ssao_vr_half`; restart callback only in stereo |
| Video owns display mode, resolution, VSync and upscale filter | `Quake/gl_vidsdl.c`, native video menu |
| Lightstyle interpolation supports off/selective/always | `Quake/gl_rmain.c`, `r_lerplightstyles` |
| Model animation, movement and turning have distinct settings | `r_lerpmodels`, `r_lerpmove`, `r_lerpturn` |
| Existing list menus already implement scrolling and pointer hits | `Quake/menu.c`, options/bind/mod browser |
| `r_vfog` registration is commented out | `Quake/gl_fog.c`; exclude rather than invent a setting |

## Incremental design

Retain the existing menu state, draw helpers, input owners and preview veil.
Replace the page split with a small categorized list: Display, Lighting,
Effects, and Advanced. Use a bounded viewport and native scrolling if a category
exceeds its row budget; keep descriptions and category navigation outside it.
One mapping must drive drawing, keyboard navigation, pointer hits and sliders.
Retain per-category selection/scroll state, clamp when availability changes,
and never use raw enum indices as on-screen row positions.

Display: gamma, contrast, desktop FOV, palette mode, world/UI filtering,
anisotropy, MSAA/mode, frame cap, link to native Video options, live preview.

Lighting: dynamic lights, native/clustered selection, shadows, AO quality,
radius/strength, VR AO evaluation, lightstyle interpolation, fullbright textures,
surface dithering. Show requested choices and a concise effective fallback
reason rather than changing the saved selection silently.

Effects: transparency/OIT, underwater effect, particles, soft particles,
particle shape, classic/enhanced models, independent model/move/turn smoothing,
screen tint if its existing behavior supports a player-facing control.

Advanced: fast sky, native sky detail/alpha/fog/wind where consumed, water/lava/
slime/teleporter opacity with correct inherit/map semantics, far clip and depth
fix, existing GPU lightmap and rendering task switches only if their meaning
can be explained accurately. Do not expose dead, cheat, debugging or no-op
settings. Avoid texture picmip until a native reload path is established.

Capabilities: unsupported options remain visible with a reason and cannot be
changed; allow desktop-specific controls to explain their VR limitation.
For MSAA reuse a narrow renderer capability query with the same format/Intel
policy as allocation rather than a second approximation. State explicitly
when a change requires applying video settings; reuse the native restart path.
Keep controls inside the existing desktop and stereo canvas bounds.

## Alternatives and verification

Rejected: a new general menu framework or independent renderer policy state.
Considered keeping two pages, but additional controls already strain the fixed
canvas. Considered expanding all menus' canvas; rejected because the mods
browser's dedicated canvas does not justify changing every menu and VR panel.

Implementation follows the completed review. All builds/tests wait until the selected feature
implementation is complete. Final checks cover category row bounds, shared
draw/hit mapping, supported MSAA choices, unsupported controls, scrolling and
mouse/controller navigation, persistence, no altered defaults, and desktop/VR
AO/backend and video-resolution boundaries.

## Astra disposition

Review ran with `gpt-6-astra`, `xhigh`; main verified those effective fields
through read-only local thread metadata. No raw session telemetry is exported.
Main spot-checked the requested-shadow default/predicate, unsupported AO reset,
MSAA callback, anisotropy maximum semantics, and liquid inheritance consumers.

| Recommendation | Disposition |
|---|---|
| Distinguish unavailable enables from an editable Off choice | Adopted: retained shadow requests can always be cleared; no implicit cross-setting changes. |
| Renderer owns effective clustered status | Adopted: narrow read-only accessor; no copied compatibility predicate in the menu. |
| MSAA choices use allocator policy; preserve anisotropy 1=maximum | Adopted: shared capability query, explicit Auto/max, requested/effective status. |
| Native AO may reset unsupported requests to zero | Adopted: document existing exception; no second hidden preference. |
| Reuse immediate callbacks, remove redundant queued restart | Adopted: native Video remains sole Test/Apply owner. |
| 320x200 canvas; 15 rows y48-167; help y176/184 | Adopted: four categories, native scroll helpers, stable IDs and shared row mapping. |
| Category change clears hover and both drag captures | Adopted: use existing M_MenuChanged and reset slider/scroll captures. |
| Preserve independent interpolation preferences and force-animation mode | Adopted: Off/Respect exclusions/Force for model animation; separate movement/turning with gating explained. |
| Texture LOD and scripted particle controls were missing | Adopted: expose existing consumed settings, not new effect implementations. |
| Exclude unconsumed sky detail and unreliable screen tint | Adopted: r_sky_quality and gl_polyblend omitted; no silent renderer behavior repair in this slice. |
| Show liquid inherit/current-map behavior and original persistence scope | Adopted: reuse native alpha query, no new liquid policy or archive changes. |
| Keep GPU lightmap/task switches outside visual menu | Adopted: advanced implementation switches remain console-owned. |
| Preview only successful meaningful edits | Adopted: unchanged/unavailable edits and navigation do not kick the reveal. |

The approved layout is Display / Lighting / Effects / Advanced. Advanced
contains consumed sky controls, liquid opacity, texture LOD, far clip and depth
fix. Effects includes a bounded particle-detail category using the same row
mechanism. No global Graphics Apply button or staged renderer settings are
introduced. The implementation gate is now complete. See final verification below.


## Final native verification

Strict Clang debug compilation and the standalone graphics/menu layout fixtures
pass. A private licensed `start` profile rendered all five graphics categories
and the 24-row mod browser. The native menu probe verified category navigation,
AO enablement, independent model/movement/turn preferences, signed texture LOD,
and hover/slider/scroll capture reset. Screenshots were inspected; long labels
are bounded to their own column, gamma shows its 0.05 steps, and empty particle
scripts are displayed as classic. The renderer reported 16x anisotropy on the
host device. This does not qualify anisotropy on a physical Steam Frame.

The same production renderer also completed ten selected-lighting cases under
an isolated two-eye simulated Monado runtime with 4x MSAA. Requested/effective
lighting, both-eye frame data, low/high dithering, transparency modes, task
rendering, CPU-lightmap/dynamics-off/shadow fallbacks, and native re-entry were
checked. No system runtime defaults, device drivers or user configurations
were changed. Physical controller ergonomics, actual gaze foveation and measured
performance remain user testing.
