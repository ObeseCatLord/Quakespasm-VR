# Weapon-wheel migration seam

The inherited wheel is still a required 2.0 behavior. A stock-weapon desktop
Vulkan slice and an OpenXR panel with controller-ray selection are
implemented. The OpenXR panel now defaults to a hand-pose-anchored playspace
placement, with the earlier view placement retained as a VR option. Its flat
Vulkan presentation is still short of the inherited 3D weapon layout. The
full mod catalogue and inherited 3D presentation remain to be ported. The source of truth is
`quakespasm-openvr/Quake/vr.c` at the pinned MAIN
commit in [migration-feature-map.md](migration-feature-map.md), particularly
its catalog and selection policy (`VR_WeaponIsOwned`,
`VR_UpdateWeaponMenuSelection`, `VR_ResolveWeaponMenuSelection`), session
capture (`VR_BeginWeaponMenu`, `VR_PrepareWeaponMenu`), and the draw/hit loop
(`VR_RunWeaponMenu`). `vr_weapon_catalog.h` and `vr_weaponmenu_hit.h` are
engine-independent helpers that can be copied with attribution. The current
target already has the shared schema parser and calibration owner in
`Quake/vr_weapon_schema.[ch]` and `Quake/vr_weapon_calibration.[ch]`.

The inherited Quick Save and Quick Load rows are now available on the shared
desktop/OpenXR panel only for a local single-client server. Their rendered
rectangles and pointer hitboxes share one layout, and release rechecks the
session before queuing the standard delayed save/load commands. Co-op player
and spawn teleport rows now use that same draw/hit layout. The client captures
a scoreboard slot, rechecks its name and eligibility on release, then queues a
1-based slot command. The server resolves the live requester and target again,
uses the donor's safe floor/hazard search through vkQuake collision queries,
and sets yaw, `fixangle` and zero velocity on relocation. The same-origin
fallback requires co-op no-player-collision and no-telefrag policies. Accepted
old-origin VR contacts are invalidated, and private snapshots carry a
relocation discontinuity epoch for prediction smoothing. Remote co-op gameplay
qualification remains open, including blocked arrival, slot changes,
room-scale alignment and replay. The 3D weapon layout also remains open.

The shared wheel loads the active mod's `wwheel.txt` roster and
`vr_weapons.txt` schema through the existing search path, with declared
selectors, impulses and ownership metadata. It ignores inherited id1 files
when the active mod did not supply them. A valid wheel roster remains
authoritative; matching schema entries enrich its metadata. Without a roster,
valid schema weapons supply the catalog. Exact-gated built-in profiles now
cover Dwell, Alkaline/Limjam, Enyo, QBJ3, Mjolnir, MG3, Hipnotic and Rogue;
the server now relays matching optional inventory fields and validated ammo
capacity stats. Runtime discovery and source special action entries still need
adapters. These are code paths pending the user's gameplay qualification.

Do not transplant source `vr.c` or its immediate-mode OpenGL drawing. Keep
vkQuake's `cl_input.c` command owner, OpenXR frame snapshot in `vr_input.c`,
`gl_screen.c` GUI task and panel setup, existing 2D draw functions, and Vulkan
alias-model draw ownership. A wheel session should capture one logical-frame
OpenXR hand/head pose and map identity when it opens, preserve the source's
retained selection while inventory/model stats lag, and cancel on map/viewentity
change or tracking loss. Selection remains local UI policy; only the final
validated weapon impulse or co-op action crosses into existing game commands.
The wheel must consume the existing calibration owner; it must not copy the
source's separate single-player and multiplayer offset tables. Whether legacy
MP schema values can become one held/muzzle offset is a separate transform
parity check, not a reason to duplicate wheel calibration state.

The smallest end-to-end proof is one stock weapon selected with a bound `+vr_weaponmenu` key on desktop
and with the bound OpenXR hand on VR, from a wheel whose hover region agrees
with the rendered item. The desktop wheel must work when no XR runtime exists.
The packaged defaults bind `VR_RIGHT_STICK_UP` for VR; desktop players may bind
the command in the key-binding menu without changing vkQuake's keyboard defaults.
Next, use the same catalog owner for Hipnotic/Rogue and mod profiles, schema
precedence, ammo/readiness, and dynamic discovery.
3D model icons, text hitboxes, outlines, and occlusion must
match the inherited wheel; a hand-anchored flat panel alone does not close VR
parity. Reuse the existing VR panel/view transforms where they fit, and add a
narrow Vulkan model/icon draw adapter only for the remaining 3D presentation.
Keep catalog, hitbox, and drawing state owned once per logical frame so
multiview does not mutate selection twice.

Before any broad port, compare a renderer adapter against a separate wheel
renderer. The former reuses Vulkan GUI/model passes and one input owner; the
latter would duplicate frame lifetime, depth policy, descriptors and input
state. The first is the working choice. Reopen this decision if it requires a
second task graph, persistent duplicate inventory state, or changes to desktop
vkQuake drawing outside the wheel's own opt-in UI.

Implementation completion needs stock and mod selection paths, closing/reopening
without stale hover, controller ray and mouse/stick selection, save/reconnect
behavior, and mixed desktop/VR compatibility. Build and software checks remain
implementation work. The user will perform live headset, eye-tracking and
gameplay validation after implementation; source-level reuse or a catalog-only
unit check alone does not prove wheel behavior.
