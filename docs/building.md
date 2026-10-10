# Building from source

The repository retains vkQuake's build system. OpenXR headers are included;
VR execution additionally requires an installed OpenXR loader/runtime. See
[portable Linux builds](../Packaging/Linux/README.md) for the complete Linux x86-64
and native Linux ARM64 release-packaging route.

## Linux

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

## Windows

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
