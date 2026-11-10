{
  description = "Patched and trimmed packages for the dwl desktop (dwl, slstatus, bemenu, htop, font, firmware)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; overlays = [ self.overlays.default ]; };
    in
    {
      # final.callPackage: new derivations. prev.callPackage: overrides of a
      # nixpkgs package of the same name (avoids infinite recursion).
      overlays.default = final: prev: {
        dwl = final.callPackage ./dwl { };
        slstatus = final.callPackage ./slstatus { };
        bemenu = prev.callPackage ./bemenu { };
        htop = prev.callPackage ./htop { };
        jetbrains-mono-nl-nerd-font = final.callPackage ./jetbrains-mono-nl-nerd-font { };
        firmware-x270 = final.callPackage ./firmware-x270 { };
      };

      packages.${system} = {
        inherit (pkgs) dwl slstatus bemenu htop jetbrains-mono-nl-nerd-font firmware-x270;
      };
    };
}
