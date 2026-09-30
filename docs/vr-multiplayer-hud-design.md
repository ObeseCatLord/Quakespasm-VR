# Multiplayer VR HUD canvas decision

The inherited OpenVR status bar draws in multiplayer on the same tracked HUD anchor as solo play. The vkQuake-based OpenXR HUD panel already draws `Sbar_Draw` once, but its eligibility originally excluded every multiplayer frame. Native deathmatch then also differs from solo: its flat status bar uses a wide `CANVAS_SBAR`, and its full scoreboard switches to `CANVAS_MENU`.

| Design option | Senior review disposition | Implementation |
| --- | --- | --- |
| Keep multiplayer HUD flat | Reject: it does not preserve inherited VR placement | Admit multiplayer to the existing HUD eligibility rules |
| Draw the scoreboard again in a second panel pass | Reject: it duplicates score dispatch and draw ordering | Keep one `Sbar_Draw`; remap `CANVAS_MENU` within the HUD panel |
| Use classic 320-by-48 HUD geometry for deathmatch | Adopt | Scope the centered `CANVAS_SBAR` mapping to the native classic HUD; suppress its flat-screen side mini-scoreboard |
| Admit modern multiplayer on its existing 640-by-400 HUD | Adopt | Map its native scores to a 416-by-200 region ending at the status bar base; the first 320 units keep their status-bar alignment |
| Preserve CSQC HUD/scores dispatch | Adopt | Keep `Sbar_DrawCSCQ` atomic and retain the existing flat fallback for explicit CSQC score/death frames |
| Move native deathmatch intermission into this patch | Defer | Intermission remains owned by the existing menu/intermission panel path |

Voice indicators use framebuffer coordinates on desktop. Within a native VR HUD, they instead use the panel's score canvas; classic voice starts 44 source units above the 48-high status bar, and both HUD styles move the list upward when many clients are speaking. The panel scope flag resets on begin/end, including the existing CSQC error cleanup path. Desktop canvas behavior is unchanged.

The Linux build validates compilation. The tracked placement, score visibility, roster overflow, and mod-specific CSQC assumptions still require later headset/gameplay verification; a 200-high scoreboard surface does not itself support an arbitrary number of players.

## Current-primary placement source checkpoint (2026-09-30)

Main compared the actual readonly primary `51b452c0:Quake/vr.c:VR_DrawSbar`
with current `SCR_VRHUDPose` and its eligibility boundary. Controller placement
uses the dominant hand, handed ±5-unit side offset including hand roll, a
roll-free panel with pitch +45 degrees, and the 10-unit panel-normal offset.
The shared HUD scale remains 0.025 by default. Other aim modes retain the
viewmodel-origin/forward anchor and the primary's pitch flattening for the two
head/mouse-yaw modes. Destination `cl.viewangles` owns gameplay aim (primary
`cl.aimangles`), so no second aim state is needed for this placement.

`R_TrackedControllerRay` supplies the existing raw controller origin; the angle
adapter uses the same view mapping and inherited gun-angle conversion. Eligibility
requires a focused rendered frame and real dominant-hand tracking in controller
mode. Multiplayer remains admitted through the native classic/modern/CSQC canvas
owners above. No demonstrated placement incompatibility was found in this
bounded source comparison, so no HUD or Vulkan rewrite is warranted. This is
source evidence only, not device visibility, arbitrary roster-size or overall VR
parity proof. End-of-implementation software qualification remains required.
