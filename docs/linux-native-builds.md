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
