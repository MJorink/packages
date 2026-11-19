# packages

glasswm, a macOS-style Wayland compositor, and patched, trimmed packages for
its desktop, as a Nix flake overlay (`overlays.default`). Used by
[MJorink/NixOS](https://github.com/MJorink/NixOS).

| Package | What |
|---|---|
| `glasswm` | macOS-style compositor (see below). Source in `glasswm/`, settings in `glasswm/config.h`. |
| `dwl` | The previous desktop, kept as a fallback: dwl 0.9, `dwl/patches/*` (bar, fair layout, focusdir, dragswap, maximize, tagfollow, focusgeom, mirror, attachbottom, tooltips, scenefx, glass, gaps, bar height, animations, touch), `dwl/config.h`. No XWayland. |
| `scenefx` | 0.5 with liquid glass blur nodes (rim refraction, dispersion, highlight). |
| `slstatus` | slstatus 1.1, no X11, battery icons, tooltip components, `slstatus/config.h`. |
| `bemenu` | nixpkgs, Wayland renderer only, Enter works in vim normal mode. |
| `htop` | nixpkgs with vim keys (`hjkl`, `gG`, `^U^D^B^F`; kill is `x`, lsof `L`). |
| `jetbrains-mono-nl-nerd-font` | 4 of the 96 font files (9 MiB). |
| `inter-variable` | Inter (SF Pro stand-in), only the upright variable font (0.9 MiB). |
| `apple-cursor-macos` | The macOS cursor theme of apple-cursor only (unfree), set as the default theme. |
| `firmware-x270` | Only the firmware an X270 loads, instead of all of linux-firmware. |

Every source is pinned by sha256. Patches are `git format-patch` files applied in
name order. Build one: `nix build .#dwl`.

Changing dwl keys/colours: edit `dwl/config.h`, push, then in the NixOS repo
`nix flake update packages && doas nixos-rebuild switch --flake .`
(or test locally with `--override-input packages ~/repos/packages`).

## glasswm

A light Wayland compositor that looks like macOS Tahoe (dark mode). It grew
out of the patched dwl above and keeps its tiling and keys:

- **Title bars** for every window that lets the compositor decorate it (foot,
  LibreWolf/Firefox, Qt, mpv, ...): liquid glass, the title centred in bold,
  red/yellow/green traffic lights with their glyphs on hover. Close, minimize
  (to the dock) and zoom (fill the screen). Drag the title to move a floating
  window or to swap a tiled one; double-click it to zoom. GTK apps draw their own
  title bars; a macOS GTK theme (e.g. WhiteSur) with
  `gtk-decoration-layout=close,minimize,maximize:` makes them match.
- **Dock**: glass, centred at the bottom. Pinned apps (`dockapps` in
  `config.h`), then running apps, then minimized windows after a separator. A dot
  marks running apps, hovering shows the name. Click focuses the app's window
  (switching tags if needed), restores a minimized one or launches the app;
  middle click opens a new one. Icons come from the apps' desktop entries and
  the icon theme (`icon_theme`), PNG or SVG.
- **Menu bar**: transparent like Tahoe's, with the Apple logo (opens
  `logomenucmd`), the focused app's name in bold, the occupied tags and the
  status on the right (slstatus, with tooltips).
- **Windows**: 16 px corners, a hairline border, a large soft shadow under the
  focused window and a lighter one under the others, fade-in and slide animations.
- **Wallpaper**: `wallpaper` in `config.h` (PNG/JPEG, scaled to cover), or a
  built-in gradient in macOS colours. No swaybg needed.
- **Tiling**: mangowm's fair layout, vim keys: `MOD+hjkl` focus,
  `MOD+SHIFT+hjkl` swap, `MOD+CTRL+hjkl` resize, `MOD+m` minimize,
  `MOD+f` zoom, `MOD+SHIFT+f` fullscreen, `MOD+\` float.

Light: one 270 KB binary on wlroots and scenefx, no XWayland, no toolkit.
Status input is the same as dwl's: `slstatus -s | glasswm -s <startup>`.

Under the hood it is dwl's single-file design: `glasswm.c` (dwl 0.9 with the
patches above applied), `deco.h` (title bars), `dock.h`, `icons.h` (desktop
entries, icon themes), `images.c` (stb_image and nanosvg, fetched pinned by
`default.nix`).
