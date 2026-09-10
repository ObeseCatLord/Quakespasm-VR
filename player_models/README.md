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

The existing QBJ3 and Enyo exclusions remain: their player animation frames are
not compatible with Ranger. These mods retain their normal player presentation,
even when a custom avatar is selected. Other total conversions also need a
compatible source-player animation frame set; adding a rig alone does not convert
an incompatible animation set.

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
Do not include held weapons in the mesh: the existing player weapon attachment
system supplies them.

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
