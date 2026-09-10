# Quakespasm VR

Quakespasm VR is a Quakespasm fork focused on VR play, mod compatibility, and
streamlined co-op. It continues gameflorist's QuakeSpasm-OpenVR lineage and VR
controls, then adds CSQC compatibility, QSS-M-inspired networking, co-op
quality-of-life behavior, modern particle and weather support, and per-mod VR
weapon calibration.

## Features

- Compatibility with popular mods such as Arcane Dimensions, Alkaline, Quake
  Brutalist Jam 3, Dwell, and the official campaigns.
- Improved VR-synchronized networking.
- A weapon wheel for both VR and desktop players.
- Streamlined co-op options, including respawning or teleporting near another
  player via the weapon wheel. Player outlines can be seen through walls when the scoreboard is shown.
- Per-mod weapon-offset calibration, with built-in profiles for popular mods.
- Support for the 2021 rerelease models and automatic rerelease discovery.
- An installed-mod browser and downloadable add-on catalogue.
- Networked VRIK so you can see other people wave at you, including full body tracking
- VOIP
- Steam Frame Support (Untested)
- Drop-in cosmetic player-model packages, selected in Multiplayer > Setup and
  synchronized by package identity. See the [custom player-model specification](player_models/README.md)
  for installation, rig requirements, and compatibility limits.

## Building from source

The repository includes the SDL2, OpenVR, and audio-codec files used by the
Windows project. Linux builds use development packages supplied by the host
distribution. Release archives do not contain Quake game data; provide your
own legally obtained `id1` directory when running the finished build.

### Linux

Install a C/C++ toolchain, GNU Make, `pkg-config`, and development packages for
SDL2, OpenGL, OpenVR, curl, libmad, libogg, Opus, Vorbis, XMP, and FLAC. For
example, on Ubuntu or Debian:

```sh
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev libgl1-mesa-dev \
  libopenvr-dev libcurl4-openssl-dev libmad0-dev libogg-dev \
  libopusfile-dev libvorbis-dev libxmp-dev libflac-dev
```

Build the release client from the repository root:

```sh
make -C Quake -f Makefile.linux clean
make -C Quake -f Makefile.linux -j"$(nproc)"
```

The main executable is `Quake/quakespasm-openvr.bin`. The small
`Quake/quakespasm-openvr` wrapper is also generated. A distributable directory
should place `quakespasm.pak` and `libopenvr_api.so` beside the executable;
other dynamically linked libraries may be supplied by the operating system.

### Windows

Install Visual Studio 2022 or Build Tools 2022 with **Desktop development with
C++**, the Windows SDK, Git, and PowerShell. The x64 Visual Studio project can
be built directly without the downloadable add-on catalogue:

```powershell
msbuild Windows\VisualStudio\quakespasm.sln `
  /p:Configuration=Release /p:Platform=x64 /m
```

For a full build with add-on downloading and spatial voice chat enabled,
install the manifest dependencies with vcpkg and pass them to MSBuild:

```powershell
git clone https://github.com/microsoft/vcpkg.git .vcpkg
git -C .vcpkg checkout cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3
.\.vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\.vcpkg\vcpkg.exe install --triplet x64-windows-static-md `
  "--x-manifest-root=$PWD" "--x-install-root=$PWD\vcpkg_installed"
msbuild Windows\VisualStudio\quakespasm.sln `
  /p:Configuration=Release /p:Platform=x64 /m `
  "/p:CurlRoot=$PWD\vcpkg_installed\x64-windows-static-md" /p:UseCurl=true `
  "/p:OpusRoot=$PWD\vcpkg_installed\x64-windows-static-md" /p:UseVoice=true
```

The executable and its runtime DLLs are written to
`Windows/VisualStudio/Build-quakespasm-sdl2/x64/Release`.

## Classic co-op

Quakespasm VR's streamlined co-op behavior is enabled by default. A server
administrator can select traditional Quake co-op behavior from the server
console or a server configuration file with:

```text
sv_coop_classic 1
```

Use `sv_coop_classic 0` to return to the modern profile. Profile-controlled
co-op feature cvars use `-1` to inherit the profile; explicitly setting one to
`0` or `1` overrides the profile for that feature.

## Default controls

Bindings can be changed under **Options > Customize Controls** or with the
console `bind` command. Mods may replace or extend actions through
`bindlist.lst`.

### VR controllers

These are the default OpenVR actions. Physical button names can vary between
controller families and SteamVR binding profiles.

Enable **Options > VR Settings > Left-handed Mode** (`vr_lefthanded 1`) to
swap the weapon hand and off-hand. The controls below swap left/right: your
left hand holds the mirrored weapon, fires, turns, and points at menus; your
right hand moves and jumps. The setting is saved. Release held buttons and
center the sticks after switching. Existing weapon/muzzle calibrations are
mirrored automatically; the `vradjust` commands work with either hand without
requiring separate offset files.

| Input | Action |
| --- | --- |
| Left stick | Move |
| Right stick left/right | Snap or smooth turn |
| Hold right stick up | Weapon wheel |
| Left trigger | Jump |
| Right trigger | Attack; point and select in menus |
| Left stick click | Run |
| Right stick click | Jump |
| Left application/menu button | Main menu |
| Right application/menu button | Next weapon |
| Left A button | Show scores |
| Right A button | Previous weapon |
| Left grip | Show scores |
| Right grip | Use / alternate fire |
