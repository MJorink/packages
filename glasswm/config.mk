VERSION = 0.1

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
DATADIR = $(PREFIX)/share

# wlroots 0.20 and scenefx 0.5. -isystem hides scenefx's -Wpedantic warnings; it is
# linked before wlroots so its wlr_scene functions override wlroots' ones.
WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.20`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.20`
FX_INCS = -isystem `$(PKG_CONFIG) --variable=includedir scenefx-0.5`/scenefx-0.5
FX_LIBS = `$(PKG_CONFIG) --libs scenefx-0.5`

CC = cc
# glasswm.c keeps upstream dwl functions this config does not bind (tile,
# monocle, zoom, ...); the compiler drops them.
CFLAGS = -O2 -pipe -Wno-unused-function
LDFLAGS = -s
