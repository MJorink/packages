# Only the 4 files that are used (NL = no ligatures, Mono = single-width icons),
# 9 MiB instead of nixpkgs' nerd-fonts.jetbrains-mono with all 96 files.
{ lib, stdenvNoCC, fetchurl }:

stdenvNoCC.mkDerivation {
  pname = "jetbrains-mono-nl-nerd-font";
  version = "3.4.0";

  src = fetchurl {
    url = "https://github.com/ryanoasis/nerd-fonts/releases/download/v3.4.0/JetBrainsMono.tar.xz";
    sha256 = "ef552a3e638f25125c6ad4c51176a6adcdce295ab1d2ffacf0db060caf8c1582";
  };

  dontUnpack = true;
  installPhase = ''
    mkdir -p $out/share/fonts/truetype
    tar -xJf $src -C $out/share/fonts/truetype \
      JetBrainsMonoNLNerdFontMono-Regular.ttf JetBrainsMonoNLNerdFontMono-Bold.ttf \
      JetBrainsMonoNLNerdFontMono-Italic.ttf JetBrainsMonoNLNerdFontMono-BoldItalic.ttf
  '';

  meta = {
    description = "JetBrains Mono NL Nerd Font Mono, 4 styles";
    license = lib.licenses.ofl;
    platforms = lib.platforms.all;
  };
}
