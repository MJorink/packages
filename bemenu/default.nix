# nixpkgs bemenu, Wayland renderer only, with patches/*
# (0001: Enter works in vim normal mode).
{ lib, bemenu }:

(bemenu.override { x11Support = false; ncursesSupport = false; }).overrideAttrs {
  patches = lib.filesystem.listFilesRecursive ./patches;
}
