# Quakespasm VR

Quakespasm VR is a [vkQuake](https://github.com/Novum/vkQuake) fork focused on VR
play, mod compatibility, and co-op. Version 2.0 brings the project to Vulkan and
OpenXR, continuing gameflorist's QuakeSpasm-OpenVR controls and VR features.
Desktop and VR players can play together.

<p>
  <img src="docs/media/vr-gameplay.gif" width="360" height="360" alt="VR gameplay with tracked weapons">
  <img src="docs/media/vr-coop.gif" width="360" height="360" alt="VR co-op with tracked player models">
</p>

## Features

- OpenXR head and controller tracking with Vulkan single-pass stereo rendering.
- vkQuake graphics and multithreaded rendering/loading, with configurable
  lighting, shadows, ambient occlusion, MSAA, and texture filtering.
- Mod compatibility, including Arcane Dimensions, Alkaline, Quake Brutalist
  Jam 3, Dwell, and the official campaigns; an installed-mod browser and
  downloadable add-on catalogue.
- Predictive multiplayer movement, VR-synchronized networking, and CSQC support.
- A weapon wheel for VR, mouse, and gamepad play, with co-op respawn and teleport
  options. Player outlines appear through walls while the scoreboard is shown.
- Built-in VR weapon calibration for popular mods, including shared profiles
  for compatible AD weapons.
- Networked player models and VRIK, cosmetic model packages, and support for
  the 2021 rerelease models.
- Voice chat, with open mic enabled by default in VR and optional push-to-talk.
- Windows x64, Linux x86-64, and Linux ARM64 support, including Steam Frame.
  Optional foveation uses eye tracking where available; fixed foveation is
  opt-in and never an automatic fallback.

## Installation

Download your platform's archive from [Releases](https://github.com/ObeseCatLord/Quakespasm-VR/releases)
and extract the complete runtime. Supply your own legally obtained Quake `id1`
directory; game data is not included. Keep your existing mods, saves, and settings.

VR requires a Vulkan-capable GPU and a working OpenXR runtime. Start with
`-openxr` for VR or `-novr` for desktop play. The packaged `quakespasm-openvr`
wrapper also accepts `-vr`. Headset resolution is controlled by the OpenXR
runtime, and eye tracking is optional.

For source builds, see [Building from source](docs/building.md).
The previous Quakespasm/OpenVR engine is preserved on
[`legacy1.0`](https://github.com/ObeseCatLord/Quakespasm-VR/tree/legacy1.0).

## Classic co-op

Streamlined co-op is enabled by default. Set `sv_coop_classic 1` on the server
for traditional Quake co-op. Regular co-op shares compatible AD jump boots;
classic co-op leaves them with the player who collected them.

## Default controls

Change bindings under **Options > Customize Controls** or with the console
`bind` command. Mods can add actions through `bindlist.lst`.
Always run is enabled by default.

### Desktop

Hold **Q** for the weapon wheel, select with the mouse, then release Q. The wheel
captures the mouse so selection does not turn the camera. With a gamepad, hold
**right stick click** and select with the right stick; the selection stick is
configurable. Gyro is off by default.

### VR controllers

| Input | Action |
| --- | --- |
| Left stick | Move |
| Right stick left/right | Snap or smooth turn |
| Right stick click; Index right touchpad touch | Weapon wheel |
| Left trigger | Jump |
| Right trigger | Attack; point and select in menus |
| Left stick click | Run/walk modifier |
| Left application/menu button | Main menu |
| Right application/menu button | Next weapon |
| Left A button; left grip | Show scores |
| Right A button | Previous weapon |
| Right grip; Index right stick click | Use / alternate fire |

Controller profiles and handedness can change physical button labels; the
bindings menu displays the active VR controls.
