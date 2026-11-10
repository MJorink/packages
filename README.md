# packages

Patched and trimmed packages for the dwl desktop, as a Nix flake overlay
(`overlays.default`). Used by [MJorink/NixOS](https://github.com/MJorink/NixOS).

| Package | What |
|---|---|
| `dwl` | dwl 0.9, `dwl/patches/*` (bar, fair layout, focusdir, dragswap, maximize, tagfollow, focusgeom, mirror, attachbottom, tooltips), `dwl/config.h`. No XWayland. |
| `slstatus` | slstatus 1.1, no X11, battery icons, tooltip components, `slstatus/config.h`. |
| `bemenu` | 0.6.23, Wayland renderer only, Enter works in vim normal mode. |
| `htop` | 3.5.3 with vim keys (`hjkl`, `gG`, `^U^D^B^F`; kill is `x`, lsof `L`). |
| `jetbrains-mono-nl-nerd-font` | 4 of the 96 font files (9 MiB). |
| `firmware-x270` | Only the firmware an X270 loads, instead of all of linux-firmware. |

Every source is pinned by sha256. Patches are `git format-patch` files applied in
name order. Build one: `nix build .#dwl`.

Changing dwl keys/colours: edit `dwl/config.h`, push, then in the NixOS repo
`nix flake update packages && doas nixos-rebuild switch --flake .`
(or test locally with `--override-input packages ~/repos/packages`).
