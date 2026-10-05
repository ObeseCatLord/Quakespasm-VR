# Quakespasm VR

Quakespasm VR is a [vkQuake](https://github.com/Novum/vkQuake) fork focused on VR
play, mod compatibility, and streamlined co-op. Version 2.0 rebases the project
on vkQuake, with Vulkan rendering and OpenXR replacing the former
Quakespasm/OpenVR base. It continues gameflorist's QuakeSpasm-OpenVR controls and
VR features, with CSQC compatibility, QSS-M-inspired predictive networking,
co-op quality-of-life behavior, modern particles, and VR weapon calibration.
Desktop and VR players can play together.

## Features

- Compatibility with popular mods such as Arcane Dimensions, Alkaline, Quake
  Brutalist Jam 3, Dwell, and the official campaigns.
- OpenXR head and controller tracking with Vulkan single-pass stereo rendering.
- vkQuake graphics and multithreaded rendering/loading, with configurable
  lighting, shadows, ambient occlusion, MSAA, and anisotropic filtering.
- Improved VR-synchronized networking and predictive multiplayer movement.
- A weapon wheel for both VR and desktop players.
- Streamlined co-op options, including respawning or teleporting near another
  player via the weapon wheel. Player outlines can be seen through walls when
  the scoreboard is shown.
- Per-mod weapon-offset calibration, with built-in profiles for popular mods
  and automatic calibration inheritance for compatible AD weapons.
- Support for the 2021 rerelease models and automatic rerelease discovery.
- An installed-mod browser and downloadable add-on catalogue.
- Networked VRIK so you can see other people wave at you, including full body
  tracking where the OpenXR runtime exposes compatible trackers.
- VOIP, with open mic enabled by default in VR and optional push-to-talk.
- Windows x64, Linux x86-64, and Linux ARM64 support, including native Steam
  Frame play and PC streaming.
- Optional runtime-supported foveation. Eye tracking is selected through the
  foveation setting; fixed foveation is opt-in and never an automatic fallback.
- Drop-in cosmetic player-model packages, selected in Multiplayer > Setup and
  synchronized by package identity.

## Installation

Download the matching engine archive from [Releases](https://github.com/ObeseCatLord/Quakespasm-VR/releases).
Extract the complete runtime, including its libraries and notices. Keep your
existing mods, saves and settings. Release archives do not contain Quake game
data; provide your own legally obtained `id1` directory.

VR requires a Vulkan-capable GPU and a working OpenXR runtime. Launch with
`-openxr`; the packaged `quakespasm-openvr` wrapper also accepts the `-vr`
argument. Use `-novr` for desktop play. The OpenXR runtime controls headset
render resolution. Eye tracking is optional; ordinary VR works without it.

`main` contains the maintained vkQuake-based engine, `2.0` remains the development
branch and updater channel, and `legacy1.0` preserves the previous
Quakespasm/OpenVR engine.

## Building from source

The repository retains vkQuake's build system. OpenXR headers are included;
VR execution additionally requires an installed OpenXR loader/runtime. See
[portable Linux builds](Packaging/Linux/README.md) for the complete Linux x86-64
and native Linux ARM64 release-packaging route.

### Linux

Install a C/C++ toolchain, Meson 1.3 or newer, Ninja, `pkg-config`, a Vulkan SDK
with headers version 1.4.341 or newer, `glslangValidator`, and `spirv-opt`.
SDL3 is preferred; SDL2 2.0.6 or newer with Vulkan support is also supported.
Audio codec, curl, and TLS development libraries enable their corresponding
optional features. For SDL2-based Ubuntu or Debian systems, the supporting
packages include:

```sh
sudo apt update
sudo apt install build-essential meson ninja-build pkg-config libsdl2-dev \
  libcurl4-openssl-dev libmpg123-dev libogg-dev libopus-dev \
  libopusfile-dev libvorbis-dev libflac-dev libgnutls28-dev
```

Install a sufficiently recent [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
separately if your distribution's headers or shader tools are older.
Build the release client from the repository root:

```sh
meson setup build --buildtype=release
meson compile -C build
```

The executable is `build/vkquake`. For a distributable package, use the portable
packaging instructions above rather than copying just the executable.

### Windows

Install Visual Studio or Build Tools with **Desktop development with C++**, the
Windows SDK, and a Vulkan SDK with headers version 1.4.341 or newer. Use the MSVC
toolset selected by the project. Build the x64 Visual Studio project:

```powershell
msbuild Windows\VisualStudio\vkquake.sln `
  /p:Configuration=Release /p:Platform=x64 /m
```

The executable and its runtime DLLs are written to
`Windows/VisualStudio/Build-vkQuake/x64/Release`.
The repository includes the SDL3 and audio-codec files used by this project.

Steam Audio is optional for source builds. Meson exposes
`use_steam_audio`, `steam_audio_include_dir`, and `steam_audio_library_dir`;
the Windows project accepts `SteamAudioSdkDir`. Include the SDK's matching
runtime library and component notices when distributing an enabled build.

## Classic co-op

Quakespasm VR's streamlined co-op behavior is enabled by default. Server
administrators can select traditional Quake co-op behavior with
`sv_coop_classic 1`. Regular co-op shares compatible AD jump boots; classic
co-op retains collector-only boots by default.

## Default controls

Bindings can be changed under **Options > Customize Controls** or with the
console `bind` command. Mods may replace or extend actions through
`bindlist.lst`.

### Desktop

Hold **Q** to open the screen-space weapon wheel, move the mouse to highlight a
weapon, then release Q to select it. The wheel captures the mouse so selection
does not turn the camera or fire a weapon. **Weapon Wheel** can be rebound in
**Options > Customize Controls**. Saved custom bindings remain authoritative.

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
| Left A button | Show scores |
| Right A button | Previous weapon |
| Left grip | Show scores |
| Right grip; Index right stick click | Use / alternate fire |

Always run is enabled by default. Controller profiles and handedness can alter
physical button labels; the bindings menu displays the active VR controls.
The 180-degree quick turn has no default binding.
