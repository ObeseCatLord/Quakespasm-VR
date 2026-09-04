{
  description = "Quakespasm-VR native development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { nixpkgs, ... }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forEachSystem = nixpkgs.lib.genAttrs supportedSystems;
    in {
      devShells = forEachSystem (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              binutils
              gcc
              gnumake
              pkg-config
            ];

            buildInputs = with pkgs; [
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
            ];
          };
        });
    };
}
