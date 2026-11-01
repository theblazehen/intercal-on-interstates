{
  description = "INTERCAL on Interstates — dev shell";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [ gcc gnumake sqlite curl gdb valgrind intercal ];
        # C-INTERCAL is the conformance oracle. Our compiler is build/ick; never
        # rely on PATH order to pick one — use $ORACLE_ICK for the reference.
        ORACLE_ICK = "${pkgs.intercal}/bin/ick";
        ORACLE_SHARE = "${pkgs.intercal}/share/ick-0.35";
      };
    };
}
