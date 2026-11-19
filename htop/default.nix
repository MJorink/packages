# nixpkgs htop with vim keys (patches/0001-vim-keys.patch).
{ lib, htop }:

htop.overrideAttrs { patches = lib.filesystem.listFilesRecursive ./patches; }
