# One weapon calibration in solo and multiplayer

The target is one held offset and one muzzle offset **per weapon**, shared by
single-player and multiplayer. A weapon's mesh and QuakeC firing expression can
still need individual calibration. Network mode should not itself select a
second physical muzzle. This is a migration target, not current `2.0` behavior.

## Evidence and current boundary

The pinned donor gates its additional `mp_*` held and muzzle vectors on
`cl.maxclients > 1` (`vr.c:3090,3134–3136,3190–3194,6197–6229`). Its client
command serializes the adjusted world muzzle relative to the player pose
(`cl_input.c:1009–1070`). The server reconstructs the remote muzzle from that
relative pose or queries the same client muzzle directly for a local player;
both routes converge on the world clamp and per-weapon QuakeC source correction
(`sv_phys.c:5386–5525`). The local shortcut avoids one frame of loopback delay
(`sv_phys.c:6465–6468`). No distinct network-mode model transform is visible in
those paths. That observation alone does not prove that every installed mod is
aligned with one vector.

Of 37 installed `vr_weapons.txt` files inspected, only QBJ3 and Enyo author MP
offsets. QBJ3 has six nonzero per-weapon muzzle deltas and zero MP held deltas;
some muzzle components exceed 45 Quake units. Enyo has a global MP muzzle delta
`(-4.995956, -7.580808, -64.77399)`. These are large enough that silently
discarding them changes shot origin. The donor's own note at `vr.c:5981` says
per-weapon MP corrections replaced a shared offset because QBJ3's weapon meshes
have different origins. That establishes the value of **per-weapon** calibration,
not a permanent need for **per-mode** calibration.

The `2.0` calibration owner currently parses the legacy fields and applies
their effective overlay when `cl.maxclients > 1`. That condition measures server
capacity: a listen host takes the local muzzle sampling path while receiving
the MP overlay. The stock `id1` shotgun has
no MP overlay. A live private OpenXR command reached an unchanged pinned donor
dedicated server, and 60 observed firing samples reconstructed its pre-clamp
muzzle from the authoritative body and command-relative pose within about
`0.00003` Quake units. This covers one stock multiplayer path, not QBJ3/Enyo
shots, a visible barrel, final projectile origin, or damage.

The `2.0` client now also has a `vrweaponsave` console command for the equipped
classic alias viewmodel. It rewrites that model's classic held/muzzle fields in
the active game's `vr_weapons.txt`, preserving unrelated blocks, enhanced
fields, and the authored per-weapon MP contribution. Existing and resulting
schema text are checked by the same parser used on reload; the exact active-game
file is checked after writing. Enhanced MD5 geometry is rejected until its
separate profile has an adjustment/save path. This is persistence for existing
classic slot values, not yet the inherited controller-driven grip/muzzle UI.
The command has been built and exercised only with a disposable schema fixture,
not against the user's installed mod files.

The senior review found a separate baseline gap: Enyo's schema carries weapon
identities and a global MP delta but omits most base held/muzzle values. The
donor initializes ten Enyo models with built-in held XYZ/scale and seeds each
muzzle Z from the held Z (`vr.c:8185–8215,3280–3294`). The `2.0` reload path
now installs those ten classic defaults before applying the Enyo schema. The
focused reload fixture checks all ten held/muzzle defaults, an identity-only
schema block, a global MP overlay, and an authored override. This restores the
calibration baseline needed for later shot-origin comparisons; it does not
prove Enyo gameplay parity by itself.

## Migration rule

Keep the existing legacy parser and effective lookup while the viewmodel,
server weapon-use, and roomscale paths are being integrated. Do not expose new
MP adjustment controls as a preferred `2.0` workflow. Use the existing
calibration owner as the sole source for both viewmodel placement and command
muzzle, rather than introducing another per-mode transform or a parallel schema
registry. A per-weapon `muzzle_source_offset` corrects the mod's QuakeC firing
expression; it is not an MP muzzle knob.

Before removing the runtime MP overlay, compare the same QBJ3 pistol and an
Enyo projectile weapon in solo, listen-server co-op, and dedicated-server
co-op. Keep the weapon, hand pose, pitch, handedness, model format, and relevant
configuration constant. Include clear space, a nearby wall, wrist rotation and
body translation. Observe the effective base/MP settings, rendered barrel,
pre/post-clamp muzzle, QuakeC trace or projectile origin and direction,
impact/damage, and world collision. Compare the pinned donor with the `2.0`
client and server. The QBJ3 pistol tracer alone is not proof of its damage
trace: the donor explicitly separates the two source expressions
(`vr.c:3777–3785`). Extend the check to flak and every distinct firing path
before retiring the overlay for all weapons. A packet or stock shotgun shot
does not establish mod parity.

If those observations show the MP vectors compensate for an old origin
mismatch, fix that mismatch at the narrow client/server or QuakeC source
boundary, then collapse each proven weapon to one calibrated held and muzzle
vector. With unchanged geometry, solo uses base `B` and co-op uses `B + Δ`;
simply dropping `Δ` or folding it into `B` changes one mode. Keep the base,
authored per-weapon delta and global delta distinguishable during conversion
so a global value is not added twice.
Legacy `mp_*` files should remain readable during migration, with a clear
diagnostic or explicit one-time conversion; avoid silently changing existing
profiles. If a mod genuinely uses different firing geometry between modes,
document the exact mod-specific behavior and keep only the minimal correction
needed at the QuakeC source boundary. Never add a global MP muzzle adjustment
to compensate for weapon-specific geometry.

Release acceptance is a mode switch between solo and multiplayer that leaves
the visible barrel and physical shot origin aligned without requiring a second
user adjustment. User-authored schema compatibility and save/reload behavior
must be checked when the legacy overlay is retired.

## Astra senior-review disposition

The local review ran with effective `gpt-6-astra` / `max` settings and checked
the donor, port and installed QBJ3/Enyo schema evidence. Its key additional
findings were the missing Enyo baseline and the listen-host transport split.

| Recommendation | Disposition |
| --- | --- |
| Keep effective MP overlays until affected mods have shot and visual parity proof | Adopted. Removing a nonzero delta now changes behavior mathematically; legacy parsing stays. |
| Compare one shared calibration through the existing renderer and weapon-use path | Adapted. Donor Enyo defaults are now installed and checked by a focused fixture; include solo, listen-host and dedicated modes so network transport and `cl.maxclients` are not conflated. |
| Convert proven entries individually while preserving provenance | Adopted. Record base, per-weapon and global contributions, preserve distinct model-format profiles, and remove only the proven redundant runtime overlay. |

No user decision is needed now. If a particular mod cannot preserve both
behaviors under one calibration after source-path correction, present that
specific tradeoff for a decision rather than silently changing its shots.
