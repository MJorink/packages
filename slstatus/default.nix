# slstatus 1.1 without X11 (prints to stdout for dwl's bar), with patches/* and config.h
{ lib, stdenv, fetchurl, configH ? ./config.h }:

stdenv.mkDerivation {
  pname = "slstatus";
  version = "1.1";

  src = fetchurl {
    url = "https://dl.suckless.org/tools/slstatus-1.1.tar.gz";
    sha256 = "7dc7140de5de61fc551c4e0feb6410afd44d78e0cdcaee45d06e5701f34eac14";
  };

  patches = lib.filesystem.listFilesRecursive ./patches;
  postPatch = "cp ${configH} config.h";

  makeFlags = [ "PREFIX=$(out)" "CC=${stdenv.cc.targetPrefix}cc" ];

  meta = {
    description = "Status monitor, no X11 (stdout for dwl's bar)";
    license = lib.licenses.isc;
    mainProgram = "slstatus";
    platforms = lib.platforms.linux;
  };
}
