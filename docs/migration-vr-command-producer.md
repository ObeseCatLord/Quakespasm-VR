# Tracked command and weapon-use migration gate

The pinned OpenVR behavior reference is `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`. Its HMD delta mapping is in `Quake/vr.c:10070–10090`; command accumulation is in `cl_main.c:2600–2630` and `cl_input.c:505–525`; relative muzzle construction is in `cl_input.c:980–1085`; grip-to-muzzle calibration and collision are in `vr.c:6190–6240`. The client subtracts the player's command-time pose origin (including accepted roomscale motion) from the calibrated world muzzle. The inherited server later restores the muzzle at the gameplay origin and uses its aim in the weapon path (`sv_phys.c:5419–5495`). Raw grip position is not the muzzle.

The `2.0` base already has a completed OpenXR frame boundary, the view owner's tracked yaw/units, the existing pending command, private VR command codec, and a PMove roomscale solver. `Quake/cl_input.c` writes the private fields; `Quake/sv_user.c` decodes and finite-checks them; `Quake/pmove.c` moves roomscale through the hull and rejects outliers. The current server does **not** have the inherited `sv_phys.c` weapon-use consumer or equivalent `vr_handpos` use. Those facts are separate from the previous locomotion smoke, which moved through ordinary command axes with `vr_active` false.

| Design | Reuse and incompatibility | Cost/risk |
| --- | --- | --- |
| Narrow adapter | Map one completed XR head/hand sample to the existing pending command. Keep yaw in `view.c`, handedness in `vr_input.c`, command lifetime in `cl_main.c`, codecs in `cl_input.c`/`sv_user.c`, and movement in `pmove.c`. Port calibration and the smallest vanilla weapon-use hook where vkQuake lacks one. | Smallest state surface; model-specific calibration and QuakeC weapon side effects still need focused work. |
| Transplant the inherited VR loop/weapon/server subsystems | Replaces or duplicates vkQuake command timing, view, network, prediction and server/QC policy just to accept different tracking structs. | Large overlap and regressions; the platform mismatch does not justify it. |

The minimal adapter is the implementation path. It may retain the inherited pure transforms and calibration tables, but must not invent a second command clock, yaw owner, renderer scene or weapon simulation. Reopen this decision if an adapter begins duplicating the server's attack state or becomes a parallel weapon protocol.

The first muzzle calibration can reuse the inherited per-weapon three-component
`vr_weapon_muzzle_offset`, its aim-space rotation, and `vr_gunmodelscale`
(`vr.c:3044–3055,6191–6229`). The current `view.c` has `vr_gunangle` and
world scale but no weapon muzzle-offset catalog. Multiplayer overlays,
enhanced-model offsets, collision adjustment, and akimbo need their existing
model/weapon dependencies mapped before activation; using a generic grip as a
stand-in would change aim and hit placement. Roomscale sampling can be
independent of that calibration, while the eventual relative muzzle and
roomscale must use the same command-time body origin.

The first vertical proof should use one ordinary vanilla weapon and an actual local private-protocol server. A known tracked HMD step must reach the command once through no-send/preview/catch-up, remain horizontal, obey world scale and mapped turning, and collide/step through PMove. A known dominant grip and its weapon-specific muzzle offset must yield a finite body-relative command pose; the server must restore the muzzle at the accepted movement origin, resolve its aim, and preserve normal QuakeC fire, ammo, sound, damage and effects. Repeat with left-handed mapping, reference rebase, focus/pose loss, public server, desktop, and an outlier. Preview must not consume or duplicate samples. Packet bytes and counters alone do not prove this behavior.

Until the calibrated muzzle and server weapon path are present and proven, the OpenXR adapter must leave `vr_active` unset. Roomscale preparation alone is not a gameplay completion claim. The inherited akimbo, contact, Gorilla, weapon wheel and mod-specific paths are later gates on the same command/weapon owners, not reasons to build a replacement service.

The first roomscale adapter now reads the existing completed HMD frame and the
view owner's effective yaw, accumulates a horizontal delta in the existing
pending command, and supplies it to both send and nonconsuming preview. It
rejects each sample or pending total above PMove's 16-unit per-command bound
without clipping. A tracking baseline resets on focus/context/reference loss
and angle locks. This is dormant on the wire while `vr_active` is false. Linux
SDL3 Meson build and the native adapter's ASan/UBSan fixture pass its mapped
delta, repeat-frame, preview, focus, outlier and lock checks. They do not prove
physical roomscale movement or weapon behavior yet.
