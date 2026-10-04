# q30 VR HUD spacing fix

## Verified reference and cause

The installed `q30a1024` CSQC source is AD-derived, but it is loaded from its
own game directory. Its `CSQC_DrawHud` layout 6 draws `Hud_DrawIBar6` as two
320-unit pictures, positioned from the centre of the full `virtsize`:
`pos + [-(640 / 2), hud_yofs]`, then a second picture at `+ [320, 0]`.
With the desktop reference configuration (`scr_sbarscale 1.6`), the normal
1024-wide canvas supplies a 640-unit virtual width, so the authored halves
occupy the two adjacent 320-unit regions of that canvas.

The VR preparation path currently replaces every CSQC HUD canvas with a
320-unit source width unless `Sbar_IsADWideCSQCHud()` accepts `-game ad` and
layout 4. q30 therefore receives 320 instead of its normal effective CSQC
source width. That game-directory/layout exception cannot cover q30 layout 6
and loses the authored horizontal coordinate space that separates the halves.

## Smallest adapter

Keep the existing `SCR_VRCSQCPanelPrepare` / `SCR_SetCSQCDisplayOverride`
boundary. Before installing the VR override, obtain the normal
`SCR_GetCSQCDisplay()` result and use its effective virtual width
(`width / scale`, floored at the classic 320-unit minimum) as the CSQC HUD
panel's source width. Continue using the existing 200-unit HUD height, donor
pose, pivot-at-horizontal-centre, finite checks, panel lifetime and CSQC error
cleanup.

This is a canvas-property adapter rather than a q30 rule: every CSQC HUD is
given the same source width that its normal canvas would expose at the active
display and scaling settings. A compact 320-unit HUD remains physically the
same size and centred; layouts that intentionally place panels relative to a
wider `virtsize` retain those offsets. No HUD QuakeC, renderer transform,
input, or dirty popup work changes.

## Acceptance and limits

Compile the existing target after the narrow `gl_screen.c` change. Source
checks must show that no q30/game-directory exception was added, the prepared
override publishes the normal effective width, and the previous dirty popup
diff is preserved. Runtime headset verification remains needed: q30 layout 6
at the installed configuration should show its two 320-unit halves side by
side with desktop-equivalent authored spacing; default and AD CSQC layouts
should remain centred and readable at nondefault HUD scales.
