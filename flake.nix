{
  description = "Quakespasm-VR native development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { nixpkgs, ... }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forEachSystem = nixpkgs.lib.genAttrs supportedSystems;
    in {
      devShells = forEachSystem (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          steamaudio = pkgs.callPackage ./nix/steamaudio.nix { };
        in {
          default = pkgs.mkShell {
            # ASan intercepts SDL2-compat's dlopen and loses the caller's RUNPATH.
            # Expose only SDL3 here, without adding another GL/GLX implementation.
            shellHook = ''
              export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [ pkgs.sdl3 ]}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            '';
            nativeBuildInputs = with pkgs; [
              binutils
              gcc
              gnumake
              pkg-config
            ];

            buildInputs = [ steamaudio ] ++ (with pkgs; [
              SDL2
              curl
              flac
              libGL
              libmad
              libogg
              libvorbis
              libxmp
              openvr
              opusfile
            ]);
          };
        });
    };
}
