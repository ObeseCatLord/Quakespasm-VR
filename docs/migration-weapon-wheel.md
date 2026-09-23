# Weapon-wheel migration seam

The inherited wheel is still a required 2.0 behavior, not an implemented
feature. The source of truth is `quakespasm-openvr/Quake/vr.c` at the pinned MAIN
commit in [migration-feature-map.md](migration-feature-map.md), particularly
its catalog and selection policy (`VR_WeaponIsOwned`,
`VR_UpdateWeaponMenuSelection`, `VR_ResolveWeaponMenuSelection`), session
capture (`VR_BeginWeaponMenu`, `VR_PrepareWeaponMenu`), and the draw/hit loop
(`VR_RunWeaponMenu`). `vr_weapon_catalog.h` and `vr_weaponmenu_hit.h` are
engine-independent helpers that can be copied with attribution. The current
target already has the shared schema parser and calibration owner in
`Quake/vr_weapon_schema.[ch]` and `Quake/vr_weapon_calibration.[ch]`.

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
Next, use the same catalog owner for Hipnotic/Rogue and mod profiles, schema
precedence, ammo/readiness, dynamic discovery, and co-op player actions.
Playspace placement, model icons, text hitboxes, outlines, and occlusion must
match the inherited wheel; an unanchored flat panel alone does not close VR
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

Completion needs observed stock and mod weapon selection, closing/reopening
without stale hover, controller ray and mouse/stick selection, save/reconnect
behavior, and a mixed desktop/VR session. Source-level reuse or a catalog-only
unit check does not prove wheel behavior. Run those acceptance checks in the
consolidated test phase requested by the user.
