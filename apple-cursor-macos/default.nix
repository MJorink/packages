# apple-cursor's black macOS theme only (21 of 34 MiB, no macOS-White), and the
# "default" theme inheriting it: dwl and clients without a theme set load that.
# Unfree like apple-cursor: allow both names in nixpkgs.config.
{ runCommand, apple-cursor }:

runCommand "apple-cursor-macos-${apple-cursor.version}" {
  meta = apple-cursor.meta // { description = "macOS cursor theme, set as the default"; };
} ''
  mkdir -p $out/share/icons/default
  cp -r ${apple-cursor}/share/icons/macOS $out/share/icons/
  printf '[Icon Theme]\nInherits=macOS\n' > $out/share/icons/default/index.theme
''
