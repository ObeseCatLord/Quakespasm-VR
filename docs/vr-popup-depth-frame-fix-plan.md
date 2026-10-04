# VR popup depth/frame fix

## Reference and scope

`SCR_DrawCenterString` is the shared renderer for server and mod
`svc_centerprint` text after `SCR_CenterPrint` wraps it. There is no separate
subtitle draw owner in this source tree; subtitle-style mod text follows that
centerprint route. Ordinary transient console text reaches `Con_DrawNotify`
through `SCR_DrawConsole`. Both paths currently use their native 2D canvases
without a physical UI-panel scope during normal VR gameplay.

The existing tracked menu adapter already supplies the correct boundary:
`SCR_SetupFrame` freezes presentation state once per XR frame, and
`GL_BeginUIPanel` combines its `world_from_ndc` matrix with the active eye's
existing multiview view/projection matrix. The popup change reuses that
boundary. It does not alter the desktop canvas code, the stereo pass lifecycle,
HUD anchors, menu input, or renderer projection code.

## Placement decision

Use a head-relative forward-facing anchor at 48 Quake units, matching the
established menu-panel orbit. `V_VRUnitsPerMetre()` maps the default world
scale to roughly 26.25 Quake units per metre, so this is approximately 1.83 m,
moving text away from the immediate eye view. The popup matrix uses the existing
`vr_menu_scale` and cancels `M_MenuCanvasScale()` just as the menu matrix does;
therefore normal `CANVAS_MENU` glyphs retain their current physical/angular
scale instead of shrinking merely because the anchor is farther than a flat
overlay. The position, basis, and matrix are sampled once in setup and then
read unchanged by both eye GUI recordings.

The active runtime extent is stable in this mapping. `GL_BeginRendering`
publishes `vid.width`/`vid.height` as `glwidth`/`glheight`; `CANVAS_MENU` maps
one source unit to `2 * M_MenuCanvasScale() / glwidth` NDC units, while the
panel maps NDC back to `vr_menu_scale / M_MenuCanvasScale() * glwidth / 2`
world units. The factors cancel, leaving `vr_menu_scale` world units per menu
source unit in both axes. The `glwidth:glheight` terms therefore preserve the
active eye render aspect ratio instead of stretching a fixed framebuffer
rectangle. `GL_BeginUIPanel` then applies each eye's existing multiview
projection; no same-NDC-per-eye placement remains.

## Implementation and acceptance

1. Publish a finite `vr_text_popup_panel` only for active, focused, tracked
   stereo frames in normal game or finale centerprint presentation. Construct
   it from the prepared head-relative `r_refdef` pose after
   `R_PrepareStereoFrame`; clear it on every other frame.
2. Scope only `SCR_CheckDrawCenterString` and the normal gameplay
   `SCR_DrawConsole`/`Con_DrawNotify` call with `GL_BeginUIPanel` and
   `GL_EndUIPanel`. Keep the existing finale/menu panel for its artwork, but
   draw the finale center string in the popup scope only when it is valid.
   Otherwise retain the original center-string draw inside the existing finale
   panel, including its physical fallback behavior.
3. Retain the original unscoped calls whenever the popup pose is unavailable or
   stereo is inactive. Leave the hand/classic/modern HUD and every unrelated
   overlay in their current panel or flat path.

Focused source checks will confirm the popup pose is prepared after stereo
setup, consumed only around the listed text paths, has balanced panel lifetime,
and compiles. Headset testing remains required to verify comfort, clipping and
both-eye readability at nondefault world/menu scales.
