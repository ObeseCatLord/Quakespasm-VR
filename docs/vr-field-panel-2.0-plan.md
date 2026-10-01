# C20: physical VR developer field panel

2026-10-01. Before-code bounded plan. Preserve native desktop diagnostics and
the native QC field collector; no executable checks until implementation ends.

## Reference and incompatibility

The primary r_showfields text is drawn inside SCR_DrawDevStats on CANVAS_INFO
(readonly primary Quake/gl_screen.c:748–786), and VR_Draw2D invokes that overlay
within the inherited physical HUD transform (primary vr.c:11037). 2.0 already
has the more complete native field collector and SCR_DrawEdictInfo table,
including server-VM ownership, selected edict, filtering, text and color.
Its full table is currently drawn through SCR_DrawInfoPanel on CANVAS_DEFAULT
outside the physical HUD (Quake/gl_screen.c:1100–1230/2447). Do not replace the
collector with the primary's older text collector.

The existing GL_BeginUIPanel/GL_EndUIPanel owner supplies multiview projection,
canvas transform, clipping and pipeline state. SCR_VRHUDPose supplies the
inherited controller/head placement, side offset and tilt; classic/modern HUD
preparation already uses it. Only the missing field-table physical transform
needs an adapter. Leak and short edict world labels keep their native layout.

## Implementation

1. Publish one finite diagnostic pose (target/right/down/normal, validity) during
   the existing SCR_SetupFrame panel-preparation boundary, using SCR_VRHUDPose.
   Clear validity each frame and on nonstereo/invalid frame/context. Do not
   sample tracking or mutate QC/gameplay from GUI drawing. The GUI task already
   depends on setup_frame; no new task or state machine.
2. Extend only SCR_DrawInfoPanel's existing call boundary with a physical-table
   flag; true only for the r_showfields full table, false for leak/world labels.
   Keep native width/height/text/scale computation and desktop clamps unchanged.
3. For a valid stereo diagnostic pose, position the complete table above the
   inherited HUD anchor with its lower center at that anchor. Use the existing
   configured vr_hud_scale for each native canvas unit; invert CANVAS_DEFAULT's
   actual ortho+viewport into world_from_ndc so framebuffer resolution does not
   change physical glyph size. Account for viewport/glwidth/glheight versus
   vid.width/vid.height exactly as existing HUD preparation does. No invented
   320x200 clipping, second text collector, scrolling or truncation policy.
4. GL_BeginUIPanel before table fill/text, then GL_EndUIPanel on every drawn
   physical-table path. Finite/dimension failures skip the VR table for that
   frame rather than draw malformed transforms. Reset canvas color afterward
   as native rendering already requires. Desktop uses the unchanged existing
   fill/string sequence and screen position. Keep PR_SwitchQCVM cleanup intact.

Write set: Quake/gl_screen.c only, approximately70–120 changed lines. A coding
delegate must wait until C13 releases this file; do not overlap C07 phase2.
Reopen if it needs a new renderer/UI projection owner, native collector changes,
input policy, pagination or materially exceeds the bound.

## Final acceptance

At consolidated Linux/ARM qualification, render an actual loaded QC edict table
with r_showfields in both eyes at multiple resolutions/aim modes, verify stable
physical placement/text and loss-of-pose/context reset. Confirm desktop native
position/text and other label/console/HUD/menu/wheel state restoration. Retain
native full-table contents including long tables; user may adjust scr_infoscale
and vr_hud_scale. Source integration alone is not readable-output proof.
