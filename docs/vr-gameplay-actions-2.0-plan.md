# Inherited VR gameplay actions through native commands

2026-09-30. Primary `51b452c0` exposes four direct VR menu actions: Give all
weapons (`impulse 9`), God Mode (`god`), No Clip (`noclip`) and Fly (`fly`).
Actual dispatch is `Quake/vr_menu.c:782–797`; labels are at1075–1089. Native
commands already exist (`cl_input.c:354/1298`, `host_cmd.c:989/1342/5087–5096`),
but native VR options does not expose these four actions. This is a visible
menu gap, not a missing cheat implementation or command spelling requirement.

## Narrow adapter

Add a four-row Gameplay Actions subpage, entered from existing Gameplay Setup.
This retains the main VR page's20-row layout after Weapon Setup is added. Use
the established page flag/cursor, key/draw routing and pointer/list geometry in
`Quake/menu.c`; no second menu/input, gameplay, server or command owner.
Dispatch the four fixed command strings through native `Cbuf_AddText` with a
newline. Existing command processing/forwarding/QC and deathmatch restrictions
remain authoritative. Do not mutate player flags, inventories or movement from
the menu, pretend to know remote server toggle state, or bypass native refusal.

Activation: Enter/Mouse1/A and either left/right use the selected action once,
matching the primary's directional activation. Up/down wrap cursor; Escape/
Mouse2/B returns to Gameplay Setup, preserving its cursor. Labels use Run for
impulse and Toggle for the three switches, with no synthetic on/off checkbox.
Keep the menu open as primary does. Existing sound, held-controller gating and
command-buffer capacity/order remain unchanged. Disconnected behavior is the
native command response, not a new menu permission state machine.

Only `Quake/menu.c` may change; expected at most120 net lines. Integrate after
the four weapon-controls slice, with one worker owning the file at a time.
No additional cheats, multiplayer-admin policy, default binds or physics hooks.
If broader behavior/new owner is needed, report and reopen this plan first.

## Final acceptance

Implementation/source/whitespace review now, no builds/tests/compiler or engine
probes until all implementation finishes. Final Linux/ARM qualification checks
keyboard/gamepad/menu pointer selection and back/re-entry, queued single
dispatch, local and remote command forwarding, actual ordinary mod impulse9,
god/noclip/fly native behavior and native deathmatch refusal. A failed native
command must not update a fake menu state. Desktop server/physics and normal
bindings remain native; user live headset/multiplayer tests remain separate.

This plan does not certify broader VR options parity. Preset/source-compensation
contracts are assessed separately against the actual inherited weapon owners.

## Source integration checkpoint

Four actions are source-integrated through the native queue, with a Gameplay
Setup entry and one matching four-row draw/pointer list. Main complete-diff
review verified fixed newline commands, reset/key/draw/back ordering and the
expanded gameplay enum/cvar bounds. Main requested keypad Enter consistency
with the existing native pages; the worker added it before integration. The
bounded patch is94 added/two removed lines (92 net), within120. No fake remote
state or server/QuakeC edits. Scoped whitespace checks pass; no builds/tests or
executable qualification. Final Linux/ARM/native command behavior remains open.
