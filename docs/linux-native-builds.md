# Native Linux builds

The 2.0 flake exposes the same packages for x86_64-linux and aarch64-linux:
`default`/`vkquake-vr` and `steamaudio`. The SDK derivation, source hash, alignment
fix and nixpkgs lock are reused from the inherited project. The game package
uses existing Meson with SDL3, Vulkan, voice/codecs and matching native Steam
Audio. This is a build recipe, not yet a verified ARM or portable release.

After implementation is complete, on each native host with Nix enabled:

```sh
nix build .#default
./result/bin/vkquake -basedir /path/to/quake
```

No Quake game data, mods or configured OpenXR runtime are packaged. The loader
is placed beside the executable, where the existing backend looks first; it
retains the selected system runtime and desktop operation. Host Vulkan drivers
and that runtime must be qualified at the end. A Nix store output is not a
portable AppImage/tarball.

For development, reuse the native SDK with ordinary Meson:

```sh
nix develop
meson setup build-native -Duse_sdl3=enabled -Duse_steam_audio=enabled \
  -Dsteam_audio_include_dir="$VKQUAKE_STEAMAUDIO_INCLUDE" \
  -Dsteam_audio_library_dir="$VKQUAKE_STEAMAUDIO_LIB"
meson compile -C build-native
```

Non-Nix builds retain the existing Meson options. Supply both SDK directories
for Steam Audio; `-Duse_steam_audio=disabled` builds the existing legacy sound
path. The latter is a fallback configuration, not ARM spatial-audio parity.

Foundry qualification uses an isolated source/build directory through SSH;
do not replace its server/game installation. Foundry currently has native
aarch64 C/C++/make/pkg-config/CMake, but neither Nix nor Meson/Ninja was found
on its PATH. Dependency setup and all build checks are deferred to the end of
the implementation pass. Windows builds remain deferred.

## Desktop and OpenXR launch

Ordinary desktop launch uses the existing vkQuake renderer and GPU selection:

```sh
./result/bin/vkquake -basedir /path/to/quake
```

For VR, start the selected OpenXR runtime and launch with explicit startup
negotiation, before the engine creates its Vulkan device:

```sh
./result/bin/vkquake -openxr -basedir /path/to/quake
```

The in-game `vr_enable 0` and `vr_enable 1` commands disable/re-enable sessions
on a compatible device. A desktop launch can also attach later when its actual
API, enabled extensions/features, GPU and queue meet the runtime requirements
and that runtime offers the original Vulkan binding. Otherwise the engine keeps
desktop output and explains that the process must restart with `-openxr`;
`vid_restart` only rebuilds render resources on the current device.

`-novr` disables XR selection and takes precedence over `-openxr`. Runtime
discovery failure during explicit startup can continue desktop; Vulkan
instance/device creation failures remain fatal. General live GPU replacement
and transparent device-loss recovery are deferred. Eye tracking is optional;
head/controller VR does not require it. Fixed foveation remains explicit only.

These are the implemented launch boundaries, not headset or release-package
qualification. Linux/ARM software checks follow implementation; live headset,
eye-tracking and performance checks remain user-deferred.
