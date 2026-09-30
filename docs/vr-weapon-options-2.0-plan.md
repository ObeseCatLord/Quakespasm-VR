# Inherited weapon controls in native VR options

2026-09-30. This plan precedes the bounded menu adapter. Reference: primary
`51b452c0`; native preimplementation `49810c45`. Preserve vkQuake desktop and
renderer owners and the one shared solo/multiplayer calibration policy.

## Verified gap and reuse

Primary `Quake/vr_menu.h:23–29` and `vr_menu.c:741–760` expose controller gun
angle, model pitch, model scale and model height. Native `Quake/view.c:95–98`
already defines these four archived settings and registers them at2545–2548.
Native view/input/rendering consumes them (`view.c:401`, `vr_input.c:1941–1944`,
`r_alias.c:909–912`), but native VR main/gameplay/joystick pages do not expose
them. Console-only access loses the inherited headset menu adjustment behavior.

Add one four-row Weapon Setup subpage inside the existing native VR options.
Use existing cvars, setters, draw context, key dispatch and pointer/list cursor;
no new calibration, weapon, renderer, network or settings owner. Copy primary
increments/ranges: gun angle2.5/-180..180, pitch0.5/-90..90,
scale0.05/0.1..2, height0.1/-5..5. Use native finite-value sanitization with
existing defaults32/0/1/0. Display actual finite current values; bounded edits
repair invalid/out-of-range console values. No independent multiplayer setting.

Replacing the existing VR menu or introducing a general options framework is
unnecessary. Extending the main page by four controls also crowds its layout;
one entry and the established subpage pattern is sufficient. Expected patch
at most180 net production lines, one file `Quake/menu.c`; reopen before a new
owner, more controls or broader behavior. Existing held calibration identity
guards handle global changes; do not bypass them or publish saved offsets.

## Implementation

1. Add Weapon Setup row, page flag/cursor and four-row cvar table in `menu.c`.
2. Reset page entry state with existing VR page flags. Route draw/key handling
   before main-page dispatch. Mouse/controller-pointer rows share the same
   `M_Mouse_UpdateListCursor` geometry; no second hit-test implementation.
3. Reuse finite clamping and native cvar setters for left/right/enter actions.
   Escape/Mouse2/B returns to the main VR page with its retained cursor.
4. Review exact reference ranges, draw/click geometry and enum/array alignment.
   Commit implementation separately from this plan.

MSAA remains native `vid_fsaa` in Graphics Options, shared by desktop and VR;
do not create an OpenGL sample-count query or duplicate VR AA policy. Primary
Enabled is already noninteractive (`vr_menu.c:653–658`), so its missing row is
not a missing toggle contract. Viewkick is not a primary menu control. The
Gun Model Offsets selector and Projectile Spawn Z need separate actual-owner
source assessment; this patch neither removes their requirements nor claims
replacement equivalence. FBT rescan and deferred locomotion follow current
scope/source dispositions, not additional work in this adapter.

## End-of-implementation acceptance

No builds/tests or engine probes until all goal implementation is finished.
At final Linux/ARM qualification, exercise keyboard, native gamepad and menu
pointer entry/edit/back/re-entry; check each exact bound/increment, finite
recovery, archive output and active-calibration cancellation. Confirm both
tracked hands and shared solo/multiplayer presentation consume the same values,
and ordinary desktop options and graphics behavior remain native. Live headset
and performance trials remain user work outside this goal.

Source integration and whitespace checks alone do not certify these behaviors.

## Existing menu-scale range correction

The same primary-control comparison found native menu-scale adjustment capped
at0.30 versus primary `vr_menu.c:767–768` range0.05..0.6. Native panel and
view-locked wheel consume finite positive scale (`gl_screen.c:1807/1992`)
through shared geometry without a0.30 ceiling; no source incompatibility
justifies halving the inherited selectable size. Correct only the native
VR_OPT_MENU_SCALE adjustment upper bound to0.60, keeping its existing rounding,
step, draw/ray owner and saved cvar. This routine range repair adds no new
control or policy. Final qualification includes both maximum panel sizes and
pointer agreement; no executable check now.

The one-line range repair is source-integrated and main-reviewed against the
exact primary range and current shared panel consumers. Scoped whitespace
check passes; no executable checks or broader panel parity claim.

## Source integration checkpoint

The bounded adapter is source-integrated:109 added production lines in
`Quake/menu.c`, reusing the existing cvars and four-row table for both adjustment
and display. Main reviewed the complete diff, primary bounds/steps, native
registration/consumers, subpage reset/dispatch/back paths and one matching
draw/pointer row geometry. Native main page now has20 rows at MENU_TOP40 with
CHARACTER_SIZE8; its last row occupies192..200 in the native200-unit canvas.
This adds no unrelated renderer or calibration changes. Scoped whitespace
check passes; final Linux/ARM software and user live qualification remain as
specified above. No builds/tests or executable probes were run.
