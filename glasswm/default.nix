# glasswm: a light macOS-style Wayland compositor (dwl 0.9 based, see
# glasswm.c's header). Built against a trimmed wlroots without XWayland and
# the packages repo's scenefx (liquid glass).
{ lib, stdenv, fetchurl, pkg-config, wayland-scanner, wayland, wayland-protocols
, libinput, libxkbcommon, libdrm, pixman, fcft, tllist, fontconfig, freetype
, wlroots_0_20, scenefx, libGL, configH ? ./config.h }:

let
  # Only what glasswm uses: no XWayland, no X11/Wayland-nested backends.
  wlroots = (wlroots_0_20.override { enableXWayland = false; }).overrideAttrs (old: {
    mesonFlags = (old.mesonFlags or [ ]) ++ [ "-Dbackends=drm,libinput" ];
  });
  # Must use the same wlroots: it replaces wlroots' scene API in the process.
  scenefx' = scenefx.override (prev: {
    scenefx = prev.scenefx.override { wlroots_0_20 = wlroots; };
  });
  # Single-header image decoders for the dock icons and the wallpaper
  stb_image = fetchurl {
    url = "https://raw.githubusercontent.com/nothings/stb/9600522de53973336fa033a289858a4354cbb04c/stb_image.h";
    sha256 = "deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91";
  };
  nanosvg = fetchurl {
    url = "https://raw.githubusercontent.com/memononen/nanosvg/239e102ec2c691f2902e20ace2ed36ee4a35cfe6/src/nanosvg.h";
    sha256 = "e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0";
  };
  nanosvgrast = fetchurl {
    url = "https://raw.githubusercontent.com/memononen/nanosvg/239e102ec2c691f2902e20ace2ed36ee4a35cfe6/src/nanosvgrast.h";
    sha256 = "79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8";
  };
in

stdenv.mkDerivation {
  pname = "glasswm";
  version = "0.1";

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.difference ./. (lib.fileset.unions [
      ./default.nix
      (lib.fileset.maybeMissing ./vendor)
    ]);
  };

  postPatch = ''
    cp ${configH} config.h
    mkdir -p vendor
    cp ${stb_image} vendor/stb_image.h
    cp ${nanosvg} vendor/nanosvg.h
    cp ${nanosvgrast} vendor/nanosvgrast.h
  '';

  nativeBuildInputs = [ pkg-config wayland-scanner ];
  buildInputs = [
    wlroots scenefx' wayland wayland-protocols libinput libxkbcommon libdrm
    pixman fcft tllist fontconfig freetype libGL # GLES2 headers for scenefx
  ];

  makeFlags = [
    "PREFIX=$(out)"
    "CC=${stdenv.cc.targetPrefix}cc"
    "PKG_CONFIG=${stdenv.cc.targetPrefix}pkg-config"
    "WAYLAND_SCANNER=wayland-scanner"
  ];

  passthru.providedSessions = [ "glasswm" ];

  meta = {
    description = "A light macOS-style Wayland compositor: liquid glass, title bars, dock, fair layout";
    license = lib.licenses.gpl3Only;
    mainProgram = "glasswm";
    platforms = lib.platforms.linux;
  };
}
