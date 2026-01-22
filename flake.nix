{
  description = "TilEm - TI calculator emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          tilem = pkgs.callPackage ./package.nix { };
          static-analysis = pkgs.buildEnv {
            name = "tilem-static-analysis";
            paths = [
              pkgs.clang-tools
              pkgs.cppcheck
            ];
          };
          default = self.packages.${system}.tilem;
        }
      );
    };
}
