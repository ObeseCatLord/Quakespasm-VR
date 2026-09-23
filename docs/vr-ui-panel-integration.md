# OpenXR menu and HUD presentation on the vkQuake UI pass

Status: architecture decision and implementation gates, 2026-09-23. This is
not a parity or hardware certification.

The inherited VR renderer places menu content on one physical panel. Both eyes
see that panel from their own views, and the controller ray uses the same panel
surface for hover and activation. The `2.0` renderer already records Quake UI
draw calls in `SCR_DrawGUI` and has a multiview-capable UI pass. The migration
should adapt those calls at the canvas/presentation boundary. It should not
create a second menu implementation.

## Verified boundaries

| Boundary | Code evidence | Consequence |
| --- | --- | --- |
| Existing UI owner | `Quake/gl_screen.c:1454-1512` draws menu, console, loading, HUD and overlays in `SCBX_GUI`. | Retain vkQuake draw calls and menu policy. |
| Canvas mapping | `Quake/gl_draw.c:1211-1290` sets both orthographic constants and Vulkan viewports, including partial status/corner canvases. | A matrix-only replacement would misplace and unclamp content. |
| Pass/shaders | `Quake/r_passes.c:1240-1283` has UI/postprocess subpasses; `Quake/gl_rmisc.c:3027-3080` swaps stereo shaders only for scene passes; xBR uses a separate vertex module around line 3236. | Keep fullscreen upscale/postprocess in screen space; panelize only selected GUI pipelines. |
| Clipping capability | The UI pass has no stencil attachment, and `Quake/gl_vidsdl.c:1884-1894` does not enable `shaderClipDistance`. | Do not assume hardware clip planes or an existing stencil mask; the direct adapter must provide panel-local clipping on every affected UI pipeline, with a portable path for Linux ARM. |
| Task ownership | `Quake/gl_screen.c:1570-1645` runs screen update on the main owner but setup and GUI may be tasks. | Task dependencies order rendering, not key/menu callbacks. |
| XR sample timing | `Quake/host.c:1051-1064` calls `VR_InputCommands` before rendering; `Quake/gl_vidsdl.c:4075` obtains a new XR frame in `GL_BeginRendering`. | Menu trigger handling must not consume both old and new samples or emit two edges. |
| Current trigger policy | `Quake/vr_input.c:552-645` maintains a per-hand trigger hysteresis and maps the right menu trigger to `K_ENTER`, except binding capture. | Extend this owner for pointer activation; do not introduce another trigger latch. |
| Existing menu hit regions | `Quake/menu.c:651-688` and `:5356-5537` derive hover, cursor changes, click validity and slider behavior from `m_mouse_x/y`; `M_UpdateMouse` is called from `Quake/host.c:1091`. `Quake/gl_draw.c:1235-1241` can expose menu coordinates outside 320×200 when scaled/letterboxed. | Feed VR ray coordinates into this menu owner using the actual displayed canvas bounds. Do not copy the donor's per-menu hit geometry or hard clamp to 320×200. |
| Donor behavior | `../quakespasm-openvr/Quake/vr.c:10539-10713` shares a first-eye panel, omits flat crosshair, and draws a separate aim/hand-relative status surface. `:11088-11190` implements trigger/pointer behavior. | Menu/console is the first proof; the HUD needs a later surface using the same adapter before parity is claimed. |
| Startup | `Quake/view.c:1379-1396` skips world rendering when the console is forced; the donor sets menu camera matrices explicitly. | Prepare eye cameras without depending on a world draw from the same or prior frame. |

## Architecture comparison

| Design | Reuse | New state/cost | Decision |
| --- | --- | --- | --- |
| Scoped panel transform in existing UI pass | Existing draw calls, canvases, pass and textures | One immutable panel snapshot, targeted shader variants, canvas clip transform | **Adopt** for the first vertical proof. |
| Flat offscreen UI texture composited onto a quad | Existing 2D raster layout | Extra target, transitions, lifetime and resolution policy; pointer timing still required | Defer unless preserving vkQuake canvas clipping through the direct adapter proves wider than expected. |
| OpenXR composition quad | Existing 2D raster layout | Additional XR swapchain and compositor ordering policy | Defer; no demonstrated need in the initial slice. |
| Globally convert UI shaders to stereo | Minimal selection logic | Incorrectly transforms scene upscale and postprocess | Reject. |

The direct adapter must preserve desktop behavior when XR is inactive. It
must not own menu selection, key bindings, UI layout, or another input state
machine. The donor `openxr:Quake/vr_menu_anchor.h` is a reusable presentation
policy; its output and ray mapping need one coordinate adapter to vkQuake's
canvas. The panel state advances once per logical XR frame and is then frozen
for both eyes. Tracking loss, recenter, teleport, and menu exit invalidate it.

## Senior review disposition

An Astra xhigh read-only review verified the source and challenged the first
draft. The following decisions reflect main-thread spot checks of the cited
code, especially task scheduling, canvas viewport setup, and existing input
ownership.

| Review recommendation | Disposition |
| --- | --- |
| Use a scoped direct adapter in the existing UI pass. | Adopt. Prove clipping and pointer alignment before spreading it across HUD classes. |
| Put pointer activation in `SCR_SetupFrame` with the pose. | Adapt. Setup and GUI can run on workers. Main thread must own menu/key callbacks, then publish an immutable snapshot for workers. |
| Reuse the donor menu anchor and one shared render/pointer surface. | Adopt through a narrow coordinate/lifecycle adapter. Keep the original safety thresholds. |
| Preserve canvas viewport and local clipping, including xBR. | Adopt. Add explicit state restoration and same-canvas cache invalidation, including the recoverable CSQC HUD error path. |
| Prepare camera matrices even without a world render. | Adopt by extracting/reusing vkQuake's existing matrix construction, not by implementing a second camera. |
| Start with menu/console, then wrist HUD. | Adopt as implementation order. **Full release parity still requires the inherited HUD presentation and interaction semantics.** |

A second, narrow Astra xhigh review resolved the task/input ordering after
checking `tasks.h`, the screen refresh guard, the menu hit path, and the host
input loop:

| Scheduling recommendation | Disposition |
| --- | --- |
| Prepare the view on the main thread before creating tasks. | Reject. Stereo setup allocates renderer resources after the begin task, so the existing begin-to-setup order is required. |
| Split submissions and join setup on main before menu activation. | Keep as a fallback. The task API permits it, but it adds a join and still does not know the menu's current hit regions until `M_Draw` runs. |
| Prepare the panel/ray in existing setup, let GUI build the hover hit, then activate on main after XR teardown. | **Adopt.** It preserves task parallelism and validates a click against the panel and menu actually drawn from that sample. The resulting menu transition is displayed in the next submission. |
| Use the prior completed XR frame's pointer hit in the host input pass. | Reject for pointer activation because the ray and moving panel could disagree with the newly displayed frame. Preserve that path for existing gameplay input. |

The UI path needs one frame/sample identity and one trigger hysteresis owner.
The host input pass must not also emit `K_ENTER` for the menu trigger that the
render-tail path owns. A held press that begins off a hit target must not
activate merely because the ray later moves onto one. Record consumption before
callbacks; a callback can open a blocking modal or change input context and
must not replay the same edge. The GUI may update pointer coordinates and hover
on its worker because those steps do not dispatch keys. Main-thread activation
must occur after `GL_EndXRFrame`, stereo restoration, and clearing
`in_update_screen`, so a modal's nested refresh can acquire new XR frames.
When the menu is not drawn, invalidate its hit rather than reusing the prior
frame's hover. Modal confirmation and binding capture stay in the same input
owner; nested input must invalidate a suspended dispatch batch even if it
returns to the original context.

## Smallest end-to-end proof and stop conditions

1. Preserve and fixture the donor anchor policy as a pure helper. Establish a
   single panel coordinate system, physical scale, view projection, inverse ray
   mapping, and explicit invalid state. Do not mutate global `glwidth` or
   `vid.conwidth` as the donor GL renderer does.
2. Resolve the input timing gap above before dispatching a menu edge. Use the
   existing `VR_InputCommands` ownership/hysteresis and preserve binding grab,
   held attack suppression, and off-panel keyboard-selection fallback. Publish
   hover before activation. If callbacks change destination/layout, revalidate
   without advancing the anchor or consuming the trigger again. Keep
   `M_UpdateMouse` from replacing a tracked VR pointer with desktop mouse
   coordinates, and reuse vkQuake's `K_MOUSE1` hit/click path where it applies.
3. In the existing UI pass, transform only menu/console draw calls from vkQuake
   canvas coordinates into the frozen physical panel. Account for both ortho
   constants and source viewport. Clip in panel-local coordinates; preserve
   fullscreen scene upscale/postprocess. Cover fills, pictures/text, and xBR.
4. Verify startup/disconnected menu in stereo and a real controller selection
   on Monado, then modal dialog, moving-head hover, one activation per press,
   binding capture, partial console, canvas changes, recenter/teleport/tracking
   loss, and desktop output. Numeric traces and compilation alone are not a
   release proof. Carry the same adapter into the hand/aim-relative HUD after
   the first menu proof.

Reopen this decision if the direct approach starts duplicating UI draw calls,
adds a parallel menu state machine, cannot preserve source-canvas clipping
without many per-call branches, or requires a second camera. At that point,
compare measured complexity with an offscreen composition target.
