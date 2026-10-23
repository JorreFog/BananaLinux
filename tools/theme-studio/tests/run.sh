#!/bin/sh
# Banana Theme Studio: all its tests.
#   sh tools/theme-studio/tests/run.sh
# expr.test.js needs g++ (it builds tests/parity.cpp against ui/src/expr.cpp); serve.test.py needs ui/banana-ui
# (it builds it if it can) and ImageMagick for the fixture; schema.test.py needs the jsonschema package.
set -e
D=$(dirname "$0")
echo "== expressions (port vs the real engine) =="
node "$D/expr.test.js"
echo
echo "== preview server (end to end) =="
python3 "$D/serve.test.py"
echo
echo "== example themes vs docs/theme.schema.json =="
python3 "$D/schema.test.py"
echo
echo "all theme-studio tests passed"