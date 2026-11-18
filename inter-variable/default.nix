# Inter (stand-in for Apple's SF Pro), only the upright variable font: one file
# with every weight, 0.9 MiB instead of nixpkgs' inter with 15 MiB.
{ runCommand, inter }:

runCommand "inter-variable-${inter.version}" {
  meta = inter.meta // { description = "Inter, upright variable font only"; };
} ''
  install -Dm444 ${inter}/share/fonts/truetype/InterVariable.ttf -t $out/share/fonts/truetype
''
