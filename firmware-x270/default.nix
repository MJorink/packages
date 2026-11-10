# Only the firmware a ThinkPad X270 loads: Intel 8265 Wi-Fi and Bluetooth,
# HD 620 (Kaby Lake) DMC/GuC/HuC. A few MiB instead of all of linux-firmware.
# A pattern that matches nothing makes the build fail (cp of the literal name).
{ runCommand, linux-firmware }:

runCommand "firmware-x270" { } ''
  cd ${linux-firmware}/lib/firmware
  for f in iwlwifi-8265-* i915/kbl_* intel/ibt-12-16.*; do
    mkdir -p "$out/lib/firmware/$(dirname "$f")"
    cp -L "$f" "$out/lib/firmware/$f"
  done
''
