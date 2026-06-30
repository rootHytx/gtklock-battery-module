{
  description = "gtklock battery status module";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "gtklock-battery-module";
        version = "4.0.0";
        src = ./.;
        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
        ];
        buildInputs = with pkgs; [
          gtk3
          glib
        ];
      };
    };
}
