#!/bin/sh
# tools/rocknix-register.py on a made-up ROCKNIX checkout in the shape of its own files: Banana's packages into the
# device's ADDITIONAL_PACKAGES, each emulator line just before its system's add_es_system (outside the per-device
# branches), never default, the cores' options in es_features.cfg; run twice, nothing doubled.
HERE=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL rocknix register: $*"; fail=1; }
B=$T/projects/ROCKNIX
mkdir -p "$B/devices/SM8550" "$B/packages/virtual/emulators" "$B/packages/ui/emulationstation/config/common"
for p in ui/banana-frontend apps/banana-apps emulators/standalone/bananads-sa emulators/libretro/bananagba-lr; do
    mkdir -p "$B/packages/$p"; : > "$B/packages/$p/package.mk"
done
printf '  if true; then\n    ADDITIONAL_PACKAGES="gamepadcalibration openabl inputplumber"\n  fi\n' > "$B/devices/SM8550/options"
cat > "$B/packages/virtual/emulators/package.mk" <<'MK'
  ### Nintendo DS
  case ${DEVICE} in
    SM8550)
      add_emu_core nds melonds melonds-sa true
      ;;
    *)
      add_emu_core nds drastic drastic-sa true
      ;;
  esac
  add_es_system nds

  add_emu_core gba retroarch mgba true
  add_emu_core gba retroarch gpsp false
  add_es_system gba
MK
cat > "$B/packages/ui/emulationstation/config/common/es_features.cfg" <<'XML'
<?xml version="1.0" encoding="UTF-8" ?>
<features>
  <emulator name="retroarch" features="ratio">
    <cores>
      <core name="mgba" features="rewind" />
    </cores>
  </emulator>
</features>
XML
python3 "$HERE/tools/rocknix-register.py" "$T" SM8550 || bad "it failed"
cp -r "$B" "$T/first"
python3 "$HERE/tools/rocknix-register.py" "$T" SM8550
diff -r "$T/first" "$B" >/dev/null || bad "a second run changed things"
grep -q 'ADDITIONAL_PACKAGES="banana-frontend banana-apps bananads-sa bananagba-lr gamepadcalibration openabl inputplumber"' "$B/devices/SM8550/options" ||
    bad "packages: $(grep ADDITIONAL "$B/devices/SM8550/options")"
grep -q 'bananafront' "$B/devices/SM8550/options" && bad "a package this checkout hasn't was added"
E=$B/packages/virtual/emulators/package.mk
[ "$(grep -B1 'add_es_system nds' "$E" | head -1 | sed 's/  #.*//')" = "  add_emu_core nds bananads bananads-sa false" ] || bad "nds: $(cat "$E")"
[ "$(grep -c 'bananads-sa' "$E")" = 1 ] || bad "nds doubled"
grep -q '^  add_emu_core gba retroarch bananagba false  # (BananaOS)$' "$E" || bad "gba: $(grep gba "$E")"
grep -q 'bananafront' "$E" && bad "an emulator whose package isn't there was listed"
F=$B/packages/ui/emulationstation/config/common/es_features.cfg
python3 -c "import xml.etree.ElementTree as E; E.parse('$F')" || bad "es_features.cfg doesn't parse"
grep -q '<core name="bananagba" features="netplay, rewind, autosave, cheevos" />' "$F" || bad "no bananagba core in es_features"
grep -q '<feature name="internal resolution">' "$F" || bad "no BananaDS option in es_features"
[ $fail = 0 ] && echo "rocknix register: all passed"
exit $fail
