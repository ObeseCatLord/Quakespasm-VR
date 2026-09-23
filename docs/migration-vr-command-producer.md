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

The producer now sets `vr_active` only for an admitted pinned peer with a
complete calibrated hand pose. The unchanged pinned-server gameplay proof is
still a release gate; roomscale preparation and a private packet alone are not
gameplay completion. The inherited akimbo, contact, Gorilla, weapon wheel and
mod-specific paths are later gates on the same command/weapon owners, not
reasons to build a replacement service.

The first roomscale adapter now reads the existing completed HMD frame and the
view owner's effective yaw, accumulates a horizontal delta in the existing
pending command, and supplies it to both send and nonconsuming preview. It
rejects tracker jumps above 16 units, but preserves the signed sum of valid
samples across no-send frames. Send and preview accept only a *whole command*
within PMove's 16-unit bound. A tracking baseline resets on focus/context/reference
loss and angle locks. It reaches the private wire only with a complete calibrated
VR command. Linux
SDL3 Meson build and the native adapter's ASan/UBSan fixture pass its mapped
delta, repeat-frame, preview, focus, outlier and lock checks. They do not prove
physical roomscale movement or weapon behavior yet.

The camera normally adds horizontal HMD displacement from a retained reference
to the player origin (`gl_rmain.c:396–422`). Once roomscale moves the body,
leaving that offset intact would count the same physical step twice. The shared
head/body contract must also cover relative muzzle construction, newer render
samples and collision-blocked movement. The pinned eye path removes current horizontal HMD translation
from the rendered eye (`vr.c:10090–10100`). The camera fixture covers its local
anchor relation; the server gameplay proof remains open.

The player-eye camera now has the narrow roomscale side of that anchor: an
admitted private peer can establish body-relative view ownership from a
prepared controller-mode VR command. The view owner retains that ownership
across temporary focus loss, missing command samples, recentering and mode
changes, so the camera never reapplies historical horizontal HMD displacement.
The existing stereo preparation then omits horizontal HMD offset and retains
floor-height and per-eye IPD placement. The latch resets on client/view reset
or loss of private admission; it does not claim the server accepted movement.
The local `2.0` server is excluded because its private move receiver is not
connected yet. Before activation, public peers and desktop keep their previous
camera behavior. The production camera fixture checks blocked and accepted
body steps, pending/send transition, focus loss, mode change, reference change,
vertical tracking, public/local and chase guards.
This does **not** yet settle the muzzle's shared origin or validate actual
movement through a private server.

The next producer slice must preserve the inherited grip-to-muzzle path. In
the donor, `SetHandPos` subtracts the current head's horizontal tracking
position from each controller before the mapped yaw rotation, then adds the
player body and floor offset (`vr.c:9397–9410,10148–10156`). In OpenXR's
right/up/back coordinate system, a hand one metre ahead of the head therefore
has positive Quake forward displacement at zero mapped yaw; its horizontal
displacement rotates with the same yaw used for roomscale. The donor adds the
selected viewmodel's calibrated `muzzle_offset`, multiplied by
`vr_gunmodelscale` and reflected in model space for left-handed play
(`vr.c:6163–6168,6191–6229`). The new pure aim-offset rotation helper preserves
the donor's `right * local.x + up * local.y + forward * local.z` arithmetic,
and a pure OpenXR hand/head helper preserves its body-relative grip geometry.
The latter takes eye height from the caller so floor and LOCAL frames can use
the same vertical reference as the camera. A third pure helper ports the donor's
model-space muzzle reflection, including wrist roll and `vr_gunmodelpitch`.
These helpers do not select the weapon or enable private commands.

The canonical `vr_weapons.txt` has a `viewmodel` keyed entry and
`muzzle_offset` for the vanilla shotgun, and mod files can override the same
schema. Reuse the donor's parser/profile precedence through native
`COM_LoadFile`/`Mem_Free` and a command-time `STAT_WEAPON`/model-precache
adapter, rather than
shipping a shotgun-only offset or a second weapon registry. The donor also
has multiplayer and enhanced-model offsets, adjustment commands, and QuakeC
source compensation; each needs its existing selection rule before general
weapon activation. For a floor-referenced OpenXR frame, hand height follows
the same floor offset as the player eye. For a LOCAL frame, eye height currently
uses the stereo camera's retained vertical reference. `R_TrackedHeadEyeHeight`
now exposes that existing reference to `V_TrackedHandBodyOffset`, so a raw grip
can share the camera's floor or LOCAL height without a second reference owner.
The production stereo-camera fixture checks a hand query before camera
preparation and a LOCAL rebase. The calibrated muzzle and command producer now
consume this raw grip; no synthetic floor height is introduced.

For the first relative-muzzle command, let `G` be the accepted dominant raw
grip offset from the current player body, `O` the selected weapon's scaled
and handed muzzle offset, and `D` the **whole-command range-accepted**
roomscale request (zero when rejected). The inherited send rule reduces to
`vr_handpos = G + O - D`, with `vr_handpos_relative = true`: the donor first
forms `player_origin + G + O` and subtracts
`player_origin + D` (`cl_input.c:1004–1018`). Collision may change the server's
final body position, so using a collision-accepted delta on the client would
be a different protocol. The command must snapshot `G`, hand rotation, weapon
profile and `D` together before send and nonconsuming preview; recomputing
one piece from a newer render frame would mix tracking times.
The smallest producer uses the existing `usercmd_t` pending record: prepare
`vr_handpos`, `vr_handrot`, `vr_handpos_relative`, and `vr_active` from one
completed frame in `VR_InputMove`, then copy those prepared values in
`VR_InputApplyPending` for both send and preview. Clear that pending pose on
each preparation and context/pose loss. The normal send reset consumes the
pending record, while preview does not. A later accumulation may replace the
pose and whole-command roomscale sum before send; each preparation must
recompute the relative muzzle against that sum.
The muzzle/hand rotation must use the dominant controller's full composed
pitch, yaw and roll (`V_TrackedMovementAngles` in follow-hand mode), even when
the movement command's view angles follow the head or offhand. The current
movement-angle path deliberately zeros roll for wire movement; using that
value for model-space muzzle reflection would lose wrist-roll calibration.
The source `vr_gunmodelpitch` and `vr_gunmodelscale` settings are registered
with their inherited defaults of 0 and 1; `held_scale` remains a separate
viewmodel presentation setting and must not alter the muzzle length.

The read-only game installation currently has 37 `vr_weapons.txt` files. A
first-token inventory found 646 `viewmodel` entries, 622 `muzzle_offset`, 41
`enhanced_muzzle_offset`, 47 `bitmask`/`impulse` pairs, 7
`muzzle_source_offset` and 6 `mp_muzzle_offset` entries. This makes full key
vocabulary and game-directory reload behavior material to real mod parity;
a hardcoded vanilla table cannot be the final calibration owner. vkQuake's
renderer already selects the active MDL/MD3/MD5 alias header through
`Mod_Extradata_CheckSkin` (`gl_model.c:199–250`, `r_alias.c:523`), so the
profile adapter should read that selection rather than make a second model
priority rule.

The bounded `VR_WeaponSchemaParse` stage now uses vkQuake's native
`COM_ParseExBuffer` tokenizer and retains the donor's full entry vocabulary,
sequential global inheritance, and 64-block limit. It rejects incomplete or
nonfinite values and does not publish a second live weapon registry. A focused
Linux fixture passes, and all 37 installed read-only schemas parse to 670
staged entries. The parser itself has no game-path or live cvar ownership.
The single calibration slot owner now registers the inherited live cvar names
once, reads the active game's `vr_weapons.txt` through `COM_LoadFile`, and
reloads after `COM_SwitchGame`. Eight source-calibrated enhanced model defaults
precede authored file fields. Missing files keep those defaults; invalid files
are rejected without publishing partial schema fields. The classic and
enhanced muzzle lookups remain separate, and the focused reload fixture passes.
Command-time model selection now reads the bounded `STAT_WEAPON` precache entry
and uses `Mod_Extradata_CheckSkin` to classify the active MDL/MD3/MD5 format,
including the MD5 8-influence path. A focused fixture covers a weapon switch
before `viewent.model` refresh. The producer prepares the calibrated relative
muzzle and full hand rotation in the existing pending command, then copies
that record to send and preview on an admitted private peer. Its sanitizer
fixture covers accepted roomscale subtraction, left/right hands, repeated
previews, public-peer exclusion and invalid pose/profile gates. The unchanged
pinned-server gameplay proof remains open; these fixtures alone do not prove
weapon effects, body movement, or collision behavior.

The inherited muzzle adds a weapon collision correction only when its
weapon-contact collision capability is enabled (`vr.c:6230–6240,
7181–7188`). The donor server advertises this capability at sign-on and can
enable it explicitly for network play; its default automatic enablement is
local singleplayer only. The migrated client has the read-only
`CL_TraceWeapon` query but no active contact producer. The first pinned-peer
proof must record the server setting and actual advertisement, and qualify
contact-enabled behavior separately. The donor server also clamps the
reconstructed muzzle against world geometry, independent of client contact
presentation.

There is also a timing boundary to measure before any head-motion prediction:
`CL_AccumulateCmd` consumes the last completed OpenXR frame in `host.c`, while
`VRXR_BeginFrame` acquires the next rendered pose later in `gl_vidsdl.c`.
This can leave the body/command one sample behind the newest displayed head
pose. The pinned-server proof should timestamp or sequence the input pose,
predicted body, authoritative body and both rendered eyes, including a blocked
step. Any render-only residual motion would need collision-safe evidence first;
adding it merely to hide latency could let an eye move through a wall.

## Local Astra senior-review disposition

| Finding | Disposition |
| --- | --- |
| The private server decoder is not on the production dispatch path | Adopted. First proof uses the unchanged pinned dedicated server; current `2.0` server authority is a later, explicit port. |
| Roomscale plus retained camera translation would double-count HMD steps | Adopted as an activation gate. Keep this command preparation dormant while one shared head/body anchor is implemented and checked. |
| Per-sample pending-total rejection invents net movement on a return step | Fixed. Preserve signed accumulation and qualify the whole command at preview/send; fixture covers `+10,+10,-10,-10`. |
| Muzzle contract needs requested origin, projectile-source compensation and both QuakeC firing scopes | Adopted in the pinned-server proof and later `2.0` server port. Start with one calibrated vanilla weapon; do not claim general weapon parity from a packet fixture. |
| Command-tag guard could restore old head translation during focus loss or a mode switch | Fixed within the existing view owner: body-relative ownership persists across transient input loss and mode changes; private-admission loss or client reset clears it. |
| Local-server exception claimed body authority before the private decoder was wired | Removed from the camera guard. Local VR remains a later server-port gate. |
| Camera fixture did not connect movement, prediction and both eyes | Adopted as an end-to-end proof gate against the unchanged pinned dedicated server once calibrated command production is wired. |

## Weapon calibration Astra review disposition

| Finding | Disposition |
| --- | --- |
| A donor held-offset registration reinitializes its slot and clears earlier enhanced defaults (`vr.c:3311,3424,6204`) | Adopted as a donor bug fix. Initialize a slot once, then update only authored fields. A file block that omits enhanced calibration must not erase a preexisting enhanced default. Gate private VR commands when the selected model format lacks a valid muzzle profile. |
| Parsed schema and runtime offset cvars are separate in the donor | Adapted. Parse the full source key vocabulary into short-lived staging records, then publish into one 99-slot mutable calibration authority; command production reads that authority, including live adjustments. No second persistent schema registry. |
| Global MP offsets can be reapplied after saved per-weapon values are reloaded | Adopted as a save/reload correction. Track authored per-weapon MP corrections separately from the effective sum; later save code must subtract the applicable global correction before writing authored values. |
| Active MDL/MD3/MD5 model selection differs from the donor | Adapted at vkQuake's `Mod_Extradata_CheckSkin` boundary, with already-loaded model data and no persistent header cache. Both `PV_MD5` and `PV_MD5_8` use the enhanced profile; MD3 retains the donor classic profile. Identify the command-time `STAT_WEAPON` model with bounds checks instead of relying on the last rendered `viewent.model`. |
| Donor file parser accepts incomplete/nonfinite vectors and overwrites profiles | Adapted. Retain the full valid-file vocabulary and inheritance rules but require complete, finite values and closing braces. Reuse native bounded tokenization. Delay wheel publication, display-model precaching, automatic schema creation, and file rewriting until their own parity slices. |
| Schema lifetime follows game-directory search paths | Adopted. Initialize after startup paths exist and reload after the existing `COM_SwitchGame` model reset. Invalidate prepared weapon references and calibration sessions on switch. |

The first calibrated proof is a classic shotgun using the canonical
`vr_weapons.txt`: its `muzzle_offset 0 0 10` is ten aim-forward units at
`vr_gunmodelscale 1`, independent of `held_scale 0.5`. Compare a weapon switch
before view refresh, normal and wall-blocked roomscale steps, and left-handed
pitch/roll against the pinned dedicated server. Observe body, both eyes,
reconstructed muzzle, firing direction, ammo, damage, sound and effects. A
packet fixture alone is insufficient. No human decision blocks this slice.

## Private producer Astra senior-review disposition

| Finding | Disposition |
| --- | --- |
| A private command can send attack without a valid VR muzzle, causing body-origin firing under controller aim | Adopted. The shared command-finalization path suppresses attack for a living, tracked pinned-peer controller-aim player when the prepared relative pose is absent. Dead-player attack remains available for respawn; focused command/preview fixtures cover the gate. |
| Enhanced fallback slot allocation could prevent classic held-only muzzle initialization | Adopted. Classic calibration availability is independent of prior enhanced slot allocation; the reload fixture includes the held-only vanilla model case. |
| The visible weapon still follows vkQuake's ordinary view transform | Adopted as a separate presentation gate. Place the tracked/calibrated weapon through the existing view-entity boundary before claiming visible gun/shot parity. A server shot alone proves only the command and weapon-use path. |
| Command displacement and reconstructed muzzle may disagree with displayed eyes during motion | Measure in the pinned-peer proof. Both donor and migrated client subtract the command's range-accepted displacement; keep that relation until simultaneous body/eye/muzzle evidence shows a correction is needed. |
| Weapon contact is advertised by the server, and default policy differs from explicit network enablement | Corrected above. Record the negotiated state and separate collision-off initial proof from contact-enabled parity. |
| Additional command clocks or model-header caches | Rejected. Existing pending command and already-loaded model selector cover the demonstrated needs. |

The next proof uses the unchanged pinned dedicated server, a confirmed classic
profile, stationary and translated firing, wall-blocked movement, accumulated
no-send movement, handedness/roll, and weapon switching. It observes both eyes,
body, reconstructed muzzle, shot direction, ammo and damage. A temporary muzzle
marker can help diagnose command geometry, but held-weapon parity requires the
actual viewmodel adapter.

The first local live slice now passes against the unchanged pinned dedicated
server using private simulated Monado and an isolated stock `id1` profile.
The client reached focused two-eye OpenXR and private signon, selected the
canonical shotgun muzzle `(0, 0, 10)`, serialized a finite relative VR attack
command from a rearmed synthetic right controller, received a covering server
ACK, and observed authoritative shells fall from 25 to 22. This is real
wire-to-gameplay evidence for basic firing, not a shot-origin or collision
proof. The input/result harness is `tests/pinned_vr_gameplay_smoke.gdb`;
the remaining geometry, movement, weapon effects and viewmodel gates above
remain open.

The donor's `Mod_Weapon` applies held offsets and scale to an alias header
(`vr.c:3100–3200`), while vkQuake's `R_AliasModelMatrix` composes each alias
instance's header scale and origin into its draw matrix (`r_alias.c:483–510`).
The latter is the narrow Vulkan presentation seam: query the existing
calibration owner for the active classic/enhanced held profile, snapshot the
result with the tracked viewmodel pose, then apply its per-instance transform
without mutating a shared model header or adding another model manager. The
same matrix helper is used by normal and diagnostic alias draws; shadow and
stereo consistency still need live qualification. The command's muzzle offset
remains governed by `vr_gunmodelscale`, not the display-only `held_scale`.
`VR_WeaponCalibrationLookupHeld` now exposes the existing slot's classic live
cvars or enhanced held offsets and the matching multiplayer overlay. It keeps
the donor's enhanced neutral scale independent of classic `held_scale`; an
enhanced slot with no held offset resolves to zero. It is data access only: the
tracked viewmodel pose and draw-matrix adapter have not yet been added.
