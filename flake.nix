{
  description = "vkQuake VR 2.0 native Linux builds";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { nixpkgs, ... }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forEachSystem = nixpkgs.lib.genAttrs supportedSystems;
      packagesFor = system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          lib = pkgs.lib;
          steamaudio = pkgs.callPackage ./nix/steamaudio.nix { };
          game = pkgs.stdenv.mkDerivation {
            pname = "vkquake-vr";
            version = "2.0-dev";
            # Only engine/build resources; no installed mods, game data, local
            # fixtures or build products enter the source closure.
            src = lib.fileset.toSource {
              root = ./.;
              fileset = lib.fileset.unions [
                (lib.fileset.fileFilter (file:
                  !(lib.hasSuffix ".o" file.name || lib.hasSuffix ".d" file.name)) ./Quake)
                ./Shaders ./Misc ./meson.build ./meson_options.txt ./LICENSE.txt
              ];
            };
            nativeBuildInputs = with pkgs; [ meson ninja pkg-config glslang spirv-tools ];
            buildInputs = [ steamaudio ] ++ (with pkgs; [
              sdl3 vulkan-headers vulkan-loader curl flac libogg libvorbis
              mpg123 opus opusfile
            ]);
            mesonFlags = [
              "-Duse_sdl3=enabled"
              "-Duse_steam_audio=enabled"
              "-Dsteam_audio_include_dir=${steamaudio}/include"
              "-Dsteam_audio_library_dir=${steamaudio}/lib"
              "-Duse_codec_mp3=enabled"
              "-Duse_codec_flac=enabled"
              "-Duse_codec_vorbis=enabled"
              "-Duse_codec_opus=enabled"
            ];
            postInstall = ''
              # SDL loads XR by absolute executable-side path first. Preserve
              # runtime selection; do not bundle a compositor or driver.
              ln -s ${pkgs.openxr-loader}/lib/libopenxr_loader.so.1 $out/bin/libopenxr_loader.so.1
              install -Dm644 "$src/Misc/vkquake.desktop" $out/share/applications/vkquake.desktop
              install -Dm644 "$src/Misc/vkQuake_256.png" $out/share/icons/hicolor/256x256/apps/vkquake.png
              install -Dm644 "$src/LICENSE.txt" $out/share/licenses/vkquake-vr/LICENSE.txt
            '';
            meta = {
              description = "vkQuake with OpenXR VR and native spatial audio";
              license = lib.licenses.gpl2Plus;
              platforms = supportedSystems;
              mainProgram = "vkquake";
            };
          };
        in { inherit steamaudio; default = game; vkquake-vr = game; };
    in {
      packages = forEachSystem packagesFor;
      devShells = forEachSystem (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          packages = packagesFor system;
        in {
          default = pkgs.mkShell {
            inputsFrom = [ packages.default ];
            packages = [ pkgs.binutils ];
            shellHook = ''
              export VKQUAKE_STEAMAUDIO_INCLUDE="${packages.steamaudio}/include"
              export VKQUAKE_STEAMAUDIO_LIB="${packages.steamaudio}/lib"
              export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [ pkgs.openxr-loader ]}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            '';
          };
        });
    };
}
