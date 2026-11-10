# dwl 0.9 with patches/* (applied in order), config.h and config.mk. No XWayland.
{ lib, stdenv, fetchurl, pkg-config, wayland-scanner, wayland, wayland-protocols
, libinput, libxkbcommon, libdrm, pixman, fcft, tllist, fontconfig, freetype
, wlroots_0_20, configH ? ./config.h }:

stdenv.mkDerivation {
  pname = "dwl";
  version = "0.9";

  src = fetchurl {
    url = "https://codeberg.org/dwl/dwl/releases/download/v0.9/dwl-v0.9.tar.gz";
    sha256 = "51ed5da49942ca3272b6f2c4f1d7641b0b947a6eb1c264dce8a65b4d245596af";
  };

  patches = lib.filesystem.listFilesRecursive ./patches;

  postPatch = ''
    cp ${configH} config.h
    cp ${./config.mk} config.mk
  '';

  nativeBuildInputs = [ pkg-config wayland-scanner ];
  buildInputs = [
    wlroots_0_20 wayland wayland-protocols libinput libxkbcommon libdrm
    pixman fcft tllist fontconfig freetype
  ];

  # Command-line values override config.mk (PREFIX=/usr/local, CC=cc there)
  makeFlags = [
    "PREFIX=$(out)"
    "CC=${stdenv.cc.targetPrefix}cc"
    "PKG_CONFIG=${stdenv.cc.targetPrefix}pkg-config"
    "WAYLAND_SCANNER=wayland-scanner"
  ];

  meta = {
    description = "dwm for Wayland, patched (bar, fair layout, tooltips, ...)";
    license = lib.licenses.gpl3Only;
    mainProgram = "dwl";
    platforms = lib.platforms.linux;
  };
}
