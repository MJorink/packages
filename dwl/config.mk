_VERSION = 0.9
VERSION  = $(_VERSION)-desktop

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

# dwl 0.9 targets wlroots 0.20 (nixpkgs: wlroots_0_20). PREFIX and CC come from makeFlags.
WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.20`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.20`

# scenefx 0.5 (patches/0012-scenefx.patch). -isystem hides its -Wpedantic warnings; it is
# linked before wlroots so its wlr_scene functions override wlroots' ones.
FX_INCS = -isystem `$(PKG_CONFIG) --variable=includedir scenefx-0.5`/scenefx-0.5
FX_LIBS = `$(PKG_CONFIG) --libs scenefx-0.5`

# No XWayland: leave these empty
XWAYLAND =
XLIBS =

CC = cc
# Optimise and strip. dwl.c keeps upstream functions this config does not
# bind (tile, monocle, zoom, ...); the compiler drops them, so silence the
# "defined but not used" warnings instead of patching them out.
CFLAGS = -O2 -pipe -Wno-unused-function
LDFLAGS = -s
