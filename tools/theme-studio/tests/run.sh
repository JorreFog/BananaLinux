#!/bin/sh
# Banana Theme Studio: all its tests.
#   sh tools/theme-studio/tests/run.sh
# expr.test.js needs g++ (it builds tests/parity.cpp against ui/src/expr.cpp); serve.test.py needs ui/banana-ui
# (it builds it if it can) and ImageMagick for the fixture; schema.test.py needs the jsonschema package.
set -e
D=$(dirname "$0")
echo "== templates.js is up to date =="
# the example themes are inlined for the offline editor, so a changed example has to regenerate it
T=$(mktemp)
cp "$D/../templates.js" "$T"
python3 "$D/../make-templates.py" >/dev/null
if ! cmp -s "$T" "$D/../templates.js"; then
    cp "$T" "$D/../templates.js"
    echo "  FAIL templates.js was stale: run python3 tools/theme-studio/make-templates.py"
    rm -f "$T"; exit 1
fi
rm -f "$T"
echo "  ok"
echo
echo "== expressions (port vs the real engine) =="
node "$D/expr.test.js"
echo
echo "== preview server (end to end) =="
python3 "$D/serve.test.py"
echo
echo "== the page in a browser =="
sh "$D/ui.test.sh"
echo
echo "== example themes vs docs/theme.schema.json =="
python3 "$D/schema.test.py"
echo
echo "all theme-studio tests passed"