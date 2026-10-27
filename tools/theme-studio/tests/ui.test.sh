#!/bin/sh
# Banana Theme Studio: the page really loads in a browser.
#
# expr.test.js runs app.js in Node, which has `global` and no DOM — so it cannot catch a browser-only mistake
# (app.js once used Node's `global` and threw before drawing anything). This loads the page in headless Chromium,
# checks the console is clean and the panels were populated, and — when the engine is built — that a preview drew.
#
#   sh tools/theme-studio/tests/ui.test.sh
#
# Skips (exit 0) when no Chromium is installed.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
PORT=8794
BASE="http://127.0.0.1:$PORT"

CHROME=""
for c in chromium chromium-browser google-chrome google-chrome-stable; do
    command -v "$c" >/dev/null 2>&1 && { CHROME=$c; break; }
done
if [ -z "$CHROME" ]; then
    echo "  (skipped: no chromium to load the page with)"
    exit 0
fi

python3 "$HERE/../serve.py" --no-browser --no-build --port $PORT >/tmp/banana-ui-test-srv.log 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
i=0
while [ $i -lt 80 ]; do
    curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
    i=$((i + 1)); sleep 0.2
done

PREVIEW=""
[ -x "$ROOT/ui/banana-ui" ] && PREVIEW="?preview=1"
DOM=/tmp/banana-ui-test-dom.html
ERR=/tmp/banana-ui-test-err.log
"$CHROME" --headless=new --disable-gpu --no-sandbox --hide-scrollbars \
    --user-data-dir=/tmp/banana-ui-test-prof --enable-logging=stderr --v=0 \
    --virtual-time-budget=12000 --window-size=1500,980 --dump-dom "$BASE/$PREVIEW" >"$DOM" 2>"$ERR" || true

fail=0
if grep -qiE 'uncaught|SyntaxError|ReferenceError|TypeError' "$ERR"; then
    echo "  FAIL the console has an error:"; grep -iE 'CONSOLE' "$ERR" | head -3; fail=1
fi
grep -q 'class="pal' "$DOM" || { echo "  FAIL the Add-elements palette didn't render"; fail=1; }
grep -q 'class="node' "$DOM" || { echo "  FAIL the Layers tree didn't render"; fail=1; }
grep -q 'id="inspector"' "$DOM" || { echo "  FAIL the inspector is missing"; fail=1; }
if grep -q 'class="scr"' "$DOM"; then
    PREVIEW=" (the live WebAssembly preview)"
elif [ -n "$PREVIEW" ]; then
    # no wasm/banana-engine.js: serve.py's stills instead
    grep -q 'class="shot' "$DOM" || { echo "  FAIL no preview shot was drawn (engine or render failed)"; fail=1; }
    grep -q 'data:image/png;base64' "$DOM" || { echo "  FAIL a preview shot has no image"; fail=1; }
fi
[ $fail = 0 ] && echo "  the page loads cleanly in $CHROME$([ -n "$PREVIEW" ] && echo "${PREVIEW# }" | sed 's/^?preview=1$/ (with a preview)/')"
exit $fail