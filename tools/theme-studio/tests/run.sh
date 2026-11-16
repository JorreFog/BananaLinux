#!/bin/sh
# Banana Theme Studio: all its tests.
#   sh tools/theme-studio/tests/run.sh
# expr.test.js needs g++ (it builds tests/parity.cpp against ui/src/expr.cpp); serve.test.py and engine.test.js need
# ui/banana-ui (they build it if they can) and ImageMagick for the fixture; browser.test.py needs Firefox; schema.test.py
# needs the jsonschema package. Each skips what it can't run.
set -e
D=$(dirname "$0")
echo "== templates.js and guide.js are up to date =="
# the example themes and the guides are inlined for the offline editor, so a changed one has to regenerate them
T=$(mktemp); G=$(mktemp)
cp "$D/../templates.js" "$T"; cp "$D/../guide.js" "$G"
python3 "$D/../make-templates.py" >/dev/null
if ! cmp -s "$T" "$D/../templates.js" || ! cmp -s "$G" "$D/../guide.js"; then
    cp "$T" "$D/../templates.js"; cp "$G" "$D/../guide.js"
    echo "  FAIL templates.js or guide.js was stale: run python3 tools/theme-studio/make-templates.py"
    rm -f "$T" "$G"; exit 1
fi
rm -f "$T" "$G"
# a ready-made theme the image ships too is the image's file: the editor started people from Phosphor Terminal as it
# was before its list was clipped (fixed in the image's copy only)
for f in "$D"/../premade/*.json; do
    s=$D/../../../system/rootfs/usr/share/banana/themes/$(basename "$f")
    [ ! -f "$s" ] || cmp -s "$f" "$s" || { echo "  FAIL premade/$(basename "$f") isn't the image's themes/$(basename "$f"): copy it over, then make-templates.py"; exit 1; }
done
echo "  ok"
echo
echo "== expressions (port vs the real engine) =="
node "$D/expr.test.js"
echo
echo "== Optimize for the handheld (optimize.js) =="
node "$D/optimize.test.js"
echo
echo "== the browser's engine (wasm) vs banana-ui =="
node "$D/engine.test.js"
echo
echo "== preview server (end to end) =="
python3 "$D/serve.test.py"
echo
echo "== the page from file://, in Firefox =="
python3 "$D/browser.test.py"
echo
echo "== the page from serve.py, in Chromium =="
sh "$D/ui.test.sh"
echo
echo "== example themes vs docs/theme.schema.json =="
python3 "$D/schema.test.py"
echo
echo "all theme-studio tests passed"