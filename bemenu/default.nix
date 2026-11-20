# nixpkgs bemenu, Wayland renderer only, with patches/*
# (0001: Enter works in vim normal mode; 0002: macOS Spotlight look, which
# draws SVG icons with nanosvg).
{ lib, bemenu, fetchurl }:

let
  nanosvg = fetchurl {
    url = "https://raw.githubusercontent.com/memononen/nanosvg/239e102ec2c691f2902e20ace2ed36ee4a35cfe6/src/nanosvg.h";
    sha256 = "e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0";
  };
  nanosvgrast = fetchurl {
    url = "https://raw.githubusercontent.com/memononen/nanosvg/239e102ec2c691f2902e20ace2ed36ee4a35cfe6/src/nanosvgrast.h";
    sha256 = "79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8";
  };
in
(bemenu.override { x11Support = false; ncursesSupport = false; }).overrideAttrs (old: {
  patches = lib.filesystem.listFilesRecursive ./patches;
  postPatch = (old.postPatch or "") + ''
    cp ${nanosvg} lib/renderers/nanosvg.h
    cp ${nanosvgrast} lib/renderers/nanosvgrast.h
  '';
})
