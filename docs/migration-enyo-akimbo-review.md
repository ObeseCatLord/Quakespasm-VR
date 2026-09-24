# Enyo paired SMGs: Astra senior review and migration decision

The next inherited akimbo vertical on the vkQuake `2.0` branch is Enyo's dual
SMGs. Keep vkQuake's renderer and desktop behavior, the existing OpenXR pair
presentation and private two-hand command, and Enyo QuakeC as the firing owner.
The installed `enyo/pak0.pak` contains an 823998-byte `progs.dat` with SHA-256
`b0d3865f1192b3858e7410ea31cbc82d88136e635b9c1d13d0aff9bbeddaeb1e`.
This is an asset identity, not an authorization based on the directory name.

The existing generated halves (`Quake/gl_model.c`), pair entity and matrix path
(`Quake/view.c`), and two-hand command (`Quake/vr_input.c`, `Quake/cl_input.c`)
remain reusable. Their present gameplay selection is deliberately restricted to
QBJ3's twin nailgun. The donor's Enyo adapter is in
`quakespasm-openvr/Quake/sv_phys.c:5033-5054,5162-5287,5348-5358`.
The loaded Enyo `W_FireSMG` calls `makevectors`, `aim`, a clearance `traceline`,
then `FireBullets2` at source statements 15324, 15344, 15352, 15363.
The QC origin calculation precedes `aim`, so a hook there alone is too late.

| Astra recommendation | Disposition |
| --- | --- |
| Port Enyo next, including paired obstruction | **Adopt.** It avoids the berserk fists' physical-melee and Dwell axe dependencies while completing a real inherited weapon. |
| Adapt pinned `makevectors`, `aim`, and clearance `traceline` calls | **Adopt.** Pin loaded program size/hash, function layout and exact call sites. Preserve QC's ammo, cadence, alternating hand, spread, damage, and effects. Read `offs` from the function's local slot, since the builtin overwrites `OFS_PARM0`. |
| Reuse the current server pose scope | **Adopt.** Add only a one-shot clearance record owned by the matching invocation. Rejected nested scopes must mask older poses; unrelated traces must not consume the record. Clear it on relocation and unwind. |
| Treat the loader recipe as server authority | **Reject.** Share immutable model geometry/anchors, but the server must independently verify its loaded QC program and weapon. |
| Extend the existing capability offer | **Adopt.** Track the complete four-slot tuple or mask in the server's successful-queue cache; a twin-only cache suppresses later Enyo changes when the twin flag remains zero. |
| Port QBJ3 fists or Dwell first, or display cosmetic pairs without gameplay | **Defer.** The first two have more dependencies; cosmetic presentation alone would imply unsupported physical gameplay. |

The smallest complete implementation reuses `V_PrepareAkimboPair`,
`VR_InputPrepareAkimboPair`, the private command, `SV_BeginPrivateVRWeaponPose`,
and `SV_EndPrivateVRWeaponPose`. Add Enyo recipe selection/contacts only where
the current twin policy is hard-coded. Apply the existing
`CL_ResolveWeaponCollision` independently to both physical hands for command
muzzles and rendered halves, under the negotiated collision authorization.
Do not copy the donor's parallel akimbo context or add another protocol.

Qualification uses installed Enyo assets and simulated tracked OpenXR to
observe both rendered halves, both physical muzzle commands, native scheduled
QC shots, impacts/damage, unchanged ammo/cadence/effects, and visible per-hand
wall retraction. Cover wall and overlapping-entity clearance, nested/lethal
callbacks, relocation, stale/tracking-loss fallback, capability changes,
program/model mismatch, handedness, weapon/map changes, and desktop/VR-disabled
QC equivalence. The user's physical headset fit test remains later. Windows and
ARM qualification remain release gates. Packet or breakpoint evidence alone
does not close gameplay parity.
