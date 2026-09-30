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

## Native multiplayer intermission adapter plan (2026-09-30)

Source comparison demonstrates a remaining placement gap: primary
`51b452c0:Quake/vr.c:VR_Draw2D` draws ordinary intermission/finale overlays
through its tracked panel without a single-player restriction. Current
`gl_screen.c:SCR_VRNativeSoloIntermission` requires `cl.maxclients == 1` and
`GAME_COOP`, leaving native multiplayer intermissions on the flat path.
Current `sbar.c:Sbar_IntermissionOverlay` dispatches ordinary co-op results
or the native deathmatch scoreboard; both draw on `CANVAS_MENU`. The existing
menu panel already owns that canvas and its physical transform. Finale also
uses `CANVAS_MENU`.

Adopt a minimal adapter: remove only the solo/gametype admission restriction
and rename the helper to describe native intermission. Keep connection/signon,
world, destination, intermission-state and CSQC-score exclusion checks. Keep
the existing prepared anchor, draw dispatch, canvas and panel cleanup. Budget:
at most eight changed production lines in `Quake/gl_screen.c`; no new panel,
scoreboard renderer, roster policy or per-eye gameplay/QC invocation.

Retaining flat native multiplayer overlays would preserve the demonstrated
placement regression. A second scoreboard pass would duplicate the existing
draw owner, so neither alternative is adopted. CSQC score/death/intermission
placement is a separate unresolved canvas boundary, not silently closed by
this native-only change. Arbitrary roster size is not certified here.

At the end of all implementation, qualify ordinary co-op results, deathmatch
scores and finale in stereo, plus unchanged desktop, native solo, menu/modal
and CSQC dispatch boundaries. No execution, build or test is authorized for
this implementation slice before that final gate.

### Native multiplayer intermission source checkpoint

The planned adapter is source-integrated at six replaced lines in
`Quake/gl_screen.c`. Main inspected the complete diff and both native
`Sbar_IntermissionOverlay` dispatches: co-op results and deathmatch scores use
the existing menu canvas; finale also uses it. Connection, signon, world,
destination, intermission-state and CSQC exclusion remain. The selector still
requires an XR render frame, so desktop dispatch is unchanged. No additional
scoreboard invocation or render pass was added. Scoped whitespace checking is
clean; no build/test/runtime result is claimed. CSQC score/death/intermission
placement remains a separate required boundary.
