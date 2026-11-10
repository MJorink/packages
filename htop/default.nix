# nixpkgs htop pinned to 3.5.3 with vim keys (patches/0001-vim-keys.patch).
{ lib, htop, fetchurl }:

htop.overrideAttrs (old: {
  version = "3.5.3";
  src = fetchurl {
    url = "https://github.com/htop-dev/htop/releases/download/3.5.3/htop-3.5.3.tar.xz";
    sha256 = "a8b164386494cb85bb255a415a3f5f80afe7a0c4491da5d113b3a0f951087e65";
  };
  patches = lib.filesystem.listFilesRecursive ./patches;
})
