# nixpkgs scenefx 0.5 with liquid glass blur nodes (patches/0001-liquid-glass.patch).
# dwl builds it against its own trimmed wlroots.
{ lib, scenefx }:

scenefx.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ lib.filesystem.listFilesRecursive ./patches;
})
