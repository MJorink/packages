.POSIX:
.SUFFIXES:

include config.mk

# vendor/ holds stb_image.h, nanosvg.h and nanosvgrast.h (default.nix copies them in)
CPPFLAGS_ = -I. -Ivendor -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(CPPFLAGS)
DEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion -Wno-format-truncation -Wno-stringop-truncation

PKGS    = wayland-server xkbcommon libinput pixman-1 fcft
CFLAGS_ = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(FX_INCS) $(CPPFLAGS_) $(DEVCFLAGS) $(CFLAGS)
LDLIBS  = `$(PKG_CONFIG) --libs $(PKGS)` $(FX_LIBS) $(WLR_LIBS) -lm $(LIBS)

HDRS = client.h config.h deco.h dock.h drwl.h icons.h ime.h images.c util.h
PROTO = cursor-shape-v1-protocol.h ext-image-copy-capture-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h

all: glasswm
# images.c (stb_image, nanosvg) is its own unit: third-party code, built without the warnings
images.o: images.c
	$(CC) -c images.c -Ivendor $(CFLAGS) -o $@
glasswm: glasswm.c images.o $(HDRS) config.mk $(PROTO)
	$(CC) glasswm.c images.o $(CFLAGS_) $(LDFLAGS) $(LDLIBS) -o $@

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
ext-image-copy-capture-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

clean:
	rm -f glasswm *.o *-protocol.h

install: glasswm
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f glasswm $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/glasswm
	cp -f glasswm.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/glasswm.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/glasswm.desktop
