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

# No XWayland: leave these empty
XWAYLAND =
XLIBS =

CC = cc
# Optimise and strip. dwl.c keeps upstream functions this config does not
# bind (tile, monocle, zoom, ...); the compiler drops them, so silence the
# "defined but not used" warnings instead of patching them out.
CFLAGS = -O2 -pipe -Wno-unused-function
LDFLAGS = -s
