# Custom player models (package version 1)

Install a complete package in `player_models/<key>/` beside `id1`, then restart
Quakespasm VR. Select it in **Multiplayer > Setup > Player model**, or use
`cl_avatar <key>`. The `player_models` console command lists installed choices
and their locations; `cl_avatar ranger` restores the standard player.

The engine also checks its startup/executable directory and its writable user
directory. A package in the selected game root takes priority over the startup
directory; the writable user directory takes priority over both. All files come
from one package directory. Packages are shared across mods, not installed inside
each mod. Installing/changing files requires an engine restart.

## Requirements and multiplayer

Custom avatars use the existing re-release Ranger animation and VR retargeting
system. Each viewing client needs its own legally obtained official Quake
re-release player data, normally found automatically from Steam or supplied with
`-rerelease <directory>`. Enabling enhanced replacement models is not required.
Quakespasm VR does not include or distribute these commercial assets.

This is a cosmetic **remote-player** replacement, not a first-person body or a new
gameplay class. It doesn't change collision, hitboxes, inventory, movement, weapon
behavior, or networked tracking. Extra unmapped bones follow their parent's pose
using their authored bind transforms; custom idle/accessory animation is not
supported in package version 1.

Servers and clients need a version supporting custom avatars. The server relays
only a validated package key and SHA-256 content identity; it does not need model
files. Players must install the identical package themselves. Missing, changed,
mismatched, or rejected packages fall back to Ranger (or the ordinary player when
re-release data is unavailable). Legacy peers receive Ranger. Different local
installation orders do not affect identity. No automatic model download occurs.

Enyo remains excluded. Other total conversions also need a compatible
source-player animation frame set; adding a rig alone does not convert an
incompatible animation set.

### QBJ3 drop-in VRIK

Install the rigged QBJ3 package as `player_models/qbj3/`, with `equipment native`
in its manifest. Restart, enable VRIK, and retain your preferred avatar setting:
QBJ3 automatically uses this **local native-player enhancement** only for live,
tracked remote `player_qbj.mdl` players. Desktop players, corpses, lost tracking,
invisibility models and missing/invalid packages retain native QBJ3 rendering.
The verified QBJ3 QC body frames use Ranger's ordinal layout; unsupported model
names or frame counts are rejected. Official re-release player data remains required.

This automatic enhancement ignores avatar selections in QBJ3, as the prior
exclusion did. It does not negotiate the local package through custom-avatar
descriptors or assert that every viewer has identical replacement bytes. Each
viewer installs their own copy, like other local enhanced-model replacements;
senders do not need the optional QBJ3 art to transmit their tracked pose.

The supplied package's gun follows the right hand and its wrench follows the
torso. These are fixed cosmetic accessories, not inventory-driven weapon swaps
or automatic left-hand mirroring. No Ranger gun/axe is added, and gameplay weapon
selection, aiming, projectile origins and collision are unchanged.

## Files

```
player_models/
  example_player/
    avatar.cfg
    model.md5mesh
    skin.tga
    skin_glow.tga   (optional)
```

`<key>` is 1–31 lowercase ASCII letters, digits, underscores or hyphens. Builtin
avatar keys and Windows reserved names such as `con` and `nul` are reserved.
Package directories and files must be ordinary files/directories, not symbolic
links or Windows reparse points. There is a maximum of 64 custom packages.

`avatar.cfg` is **data, not an executable Quake configuration**. Minimal example:

```
version 1
name "Example Player"
scale 1
```

`name` is required, quoted, printable ASCII, up to 31 characters. `scale` is
optional (default 1), a decimal number from 0.25 to 4, and changes only avatar
presentation. Optional bone remapping uses unquoted names, for example:

```
bone Hip Pelvis
bone LowerArm_L Forearm_L
```

Optional `equipment native` retains weapons/accessories authored into the mesh
and suppresses the additional Ranger weapon. Bind these props to the intended
hand or torso, including through unmapped child bones. This is a fixed cosmetic
loadout; it does not switch meshes from the player's inventory. Omit the setting,
or use `equipment ranger`, to retain the default Ranger attachment behavior.
Clients must support this directive; earlier builds reject manifests containing
it. Descriptor-selected custom avatars require identical package bytes on viewing
clients; the local QBJ3 enhancement described above does not negotiate identity.

Only the semantic names below may be remapped. Unknown/duplicate directives,
duplicate bone mappings, comments, escaped strings and paths are not accepted.
The manifest is limited to 8 KiB. Do not put commands or other settings in it.

## Mesh and rig

Use a single-surface **MD5 mesh version 10**, with shader exactly `"skin"`.
Companion `.md5anim` files and shader paths are deliberately not loaded. Author a
neutral upright humanoid bind pose, approximately Ranger-sized (about 56 Quake
units tall). A conventional coordinate system is +X forward, +Y anatomical left,
+Z up. The renderer derives presentation from Hip, Head and shoulder positions;
keep those axes nondegenerate and the left/right anatomy consistent.

Required semantic bones (these names are the defaults):

```
Hip
  Spine1
    Spine2
      Neck
        Head
      Shoulder_L -> UpperArm_L -> LowerArm_L -> Hand_L
      Shoulder_R -> UpperArm_R -> LowerArm_R -> Hand_R
  UpperLeg_L -> LowerLeg_L -> Foot_L
  UpperLeg_R -> LowerLeg_R -> Foot_R
```

Additional intermediary bones are allowed, but preserve those descendant chains.
Bone parents precede children. Joint names must be unique and at most 31 bytes;
global bind transforms must be rigid, finite, and orthonormal. Apply object scale
before exporting. Do not bake mirrored/nonuniform scale into joint matrices.
With the default equipment mode, omit held weapons from the mesh because the
player attachment system supplies them. Use `equipment native` for a character
whose authored mesh already includes its own weapons.

Limits: 256 joints, 16,384 exported vertices (including UV splits), 32,768
triangles, four referenced influences per vertex, and an 8 MiB mesh file. Every
vertex needs positive total weight, normalized to 1 within 0.001. Joint/weight
coordinates must be within ±1024 units and UVs within ±16. Malformed packages are
rejected without substituting their bind-pose mesh for a player.

## Textures

Use uncompressed true-color TGA (type 2), 24-bit RGB or 32-bit RGBA, left-origin,
with either top or bottom vertical origin. RGBA files must declare 8 alpha bits.
Standard TGA v2 footers are supported. RLE, palette images, PNG/JPEG, animated
skins, and alternate texture paths are not part of this deliberately small v1
format. Dimensions must be at most 2048 × 2048; the combined decoded RGBA size of
skin and glow must not exceed 16 MiB. Prefer a 1024 × 1024 skin for multiplayer
performance. Optional `skin_glow.tga` supplies fullbright pixels.

All four fixed files (including manifest bytes and optional-glow presence) enter
the content hash. Even changing whitespace creates a different package identity;
distribute the same finished package to everyone. The loader rechecks the bytes
before use, and texture reloads use the same owned pixels rather than reading a
different mod's texture. Rejected packages require restart after correction.

## Author checklist

Verify idle, walking, firing, crouching, death and respawn, then VR hand/head
tracking with a second client. Also check missing-package fallback and switching
back to Ranger. Excessively heavy meshes/textures multiply their cost for every
visible player, so keep assets substantially below the maximum where possible.
