# Tracked command and weapon-use migration gate

The pinned OpenVR behavior reference is `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`. Its HMD delta mapping is in `Quake/vr.c:10070–10090`; command accumulation is in `cl_main.c:2600–2630` and `cl_input.c:505–525`; relative muzzle construction is in `cl_input.c:980–1085`; grip-to-muzzle calibration and collision are in `vr.c:6190–6240`. The client subtracts the player's command-time pose origin (including roomscale motion accepted by the client's range check, not collision-accepted motion) from the calibrated world muzzle. The inherited server later restores the muzzle at the gameplay origin and uses its aim in the weapon path (`sv_phys.c:5419–5495`). Raw grip position is not the muzzle.

The `2.0` base already has a completed OpenXR frame boundary, the view owner's tracked yaw/units, the existing pending command, private VR command codec, and a **client-side** PMove roomscale solver. `Quake/cl_input.c` writes the private fields and `Quake/sv_user.c` defines a decoder that finite-checks them, but the current server dispatch still calls `SV_ReadClientMove` rather than `SV_ReadPrivateUsercmd` (`sv_user.c:949–954`). Its walking is still the ordinary `sv_phys.c` path, and it has no inherited weapon-use muzzle consumer. The previous locomotion smoke moved through ordinary command axes with `vr_active` false; it did not establish private VR server authority.

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

The first vertical proof should use one ordinary vanilla weapon and the **unchanged pinned dedicated server**. The `2.0` client already admits a pinned peer explicitly; using its existing VR movement and weapon consumers narrows this proof before porting those server owners. A known tracked HMD step must reach the command once through no-send/preview/catch-up, remain horizontal, obey world scale and mapped turning, and collide/step on the server. A known dominant grip and its weapon-specific muzzle offset must yield a finite body-relative command pose; the server must restore the muzzle at its authoritative player origin and preserve normal QuakeC fire, ammo, sound, damage and effects. Check an unobstructed step and a wall-blocked step while observing body, eye, reconstructed muzzle, shot direction, ammo and damage together. Repeat with left-handed mapping, reference rebase, focus/pose loss, simultaneous turning, public server, desktop, and an outlier. Preview must not consume or duplicate samples. Packet bytes and counters alone do not prove this behavior. Port the `2.0` server's private dispatch, authority movement and weapon hooks only after this first proof defines the behavior to preserve.

Until the calibrated muzzle and server weapon path are present and proven, the OpenXR adapter must leave `vr_active` unset. Roomscale preparation alone is not a gameplay completion claim. The inherited akimbo, contact, Gorilla, weapon wheel and mod-specific paths are later gates on the same command/weapon owners, not reasons to build a replacement service.

The first roomscale adapter now reads the existing completed HMD frame and the
view owner's effective yaw, accumulates a horizontal delta in the existing
pending command, and supplies it to both send and nonconsuming preview. It
rejects tracker jumps above 16 units, but preserves the signed sum of valid
samples across no-send frames. Send and preview accept only a *whole command*
within PMove's 16-unit bound. A tracking baseline resets on focus/context/reference
loss and angle locks. This is dormant on the wire while `vr_active` is false. Linux
SDL3 Meson build and the native adapter's ASan/UBSan fixture pass its mapped
delta, repeat-frame, preview, focus, outlier and lock checks. They do not prove
physical roomscale movement or weapon behavior yet.

The camera currently adds horizontal HMD displacement from a retained reference
to the player origin (`gl_rmain.c:396–422`). Once roomscale moves the body,
leaving that offset intact would count the same physical step twice. The next
adapter must share one head/body anchor among movement, eye placement and
relative muzzle construction, including newer render samples and collision-
blocked movement. The pinned eye path removes current horizontal HMD translation
from the rendered eye (`vr.c:10090–10100`). Do not enable `vr_active` until this
camera relation and the server gameplay proof are correct.

## Local Astra senior-review disposition

| Finding | Disposition |
| --- | --- |
| The private server decoder is not on the production dispatch path | Adopted. First proof uses the unchanged pinned dedicated server; current `2.0` server authority is a later, explicit port. |
| Roomscale plus retained camera translation would double-count HMD steps | Adopted as an activation gate. Keep this command preparation dormant while one shared head/body anchor is implemented and checked. |
| Per-sample pending-total rejection invents net movement on a return step | Fixed. Preserve signed accumulation and qualify the whole command at preview/send; fixture covers `+10,+10,-10,-10`. |
| Muzzle contract needs requested origin, projectile-source compensation and both QuakeC firing scopes | Adopted in the pinned-server proof and later `2.0` server port. Start with one calibrated vanilla weapon; do not claim general weapon parity from a packet fixture. |
