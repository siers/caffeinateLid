{
  description = "caffeinateLid: keep a Mac awake with the lid closed until interrupted";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "caffeinateLid";
          version = "0.1.0";
          src = ./.;

          buildPhase = ''
            $CC -Wall -Wextra -Werror -O2 -o caffeinateLid caffeinateLid.c
          '';

          doCheck = true;
          nativeCheckInputs = [ pkgs.coreutils pkgs.perl ];
          checkPhase = ''
            patchShebangs test.sh
            ./test.sh
          '';

          installPhase = ''
            install -Dm755 caffeinateLid $out/bin/caffeinateLid
          '';

          meta = {
            description = "Keep a Mac awake with the lid closed (pmset disablesleep) until interrupted";
            platforms = nixpkgs.lib.platforms.darwin;
            mainProgram = "caffeinateLid";
          };
        };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell { packages = [ pkgs.coreutils ]; };
      });
    };
}
