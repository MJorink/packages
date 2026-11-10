# nixpkgs bemenu pinned to 0.6.23, Wayland renderer only, with patches/*
# (0001: Enter works in vim normal mode).
{ lib, bemenu, fetchurl }:

(bemenu.override { x11Support = false; ncursesSupport = false; }).overrideAttrs (old: {
  version = "0.6.23";
  src = fetchurl {
    url = "https://github.com/Cloudef/bemenu/archive/refs/tags/0.6.23.tar.gz";
    sha256 = "0e7878c3bf57e0e362efe89c59deddd33819cd62b980be7e0b7bb4da45478e44";
  };
  patches = lib.filesystem.listFilesRecursive ./patches;
})
