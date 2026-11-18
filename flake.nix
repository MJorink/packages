{
  description = "Patched and trimmed packages for the dwl desktop (dwl, scenefx, slstatus, bemenu, htop, fonts, firmware)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; overlays = [ self.overlays.default ]; };
    in
    {
      # Overrides of a nixpkgs package of the same name get prev.<name> passed
      # in explicitly; otherwise callPackage would hand them the overlay result.
      overlays.default = final: prev: {
        dwl = final.callPackage ./dwl { };
        slstatus = final.callPackage ./slstatus { };
        bemenu = final.callPackage ./bemenu { inherit (prev) bemenu; };
        htop = final.callPackage ./htop { inherit (prev) htop; };
        scenefx = final.callPackage ./scenefx { inherit (prev) scenefx; };
        jetbrains-mono-nl-nerd-font = final.callPackage ./jetbrains-mono-nl-nerd-font { };
        inter-variable = final.callPackage ./inter-variable { };
        firmware-x270 = final.callPackage ./firmware-x270 { };
      };

      packages.${system} = {
        inherit (pkgs) dwl scenefx slstatus bemenu htop jetbrains-mono-nl-nerd-font inter-variable firmware-x270;
      };
    };
}
