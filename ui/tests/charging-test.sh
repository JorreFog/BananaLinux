#!/bin/sh
# Exercise the real second-A action against a stand-in helper, never sysfs.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${CHARGING_TEST_OUT:-$(mktemp -d)}
mkdir -p "$OUT"
if [ -z "${CHARGING_TEST_OUT:-}" ]; then trap 'rm -rf "$OUT"' EXIT HUP INT TERM; fi
"$HERE/tests/fixture.sh" "$OUT/fixture" >/dev/null
# The apostrophe and space also exercise the quoted executable path.
HELPER="$OUT/charger's helper"
cat > "$HELPER" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$CHARGING_CALLS"
case $CHARGING_RESULT in
success) echo 'Higher input limit selected for this connection.' ;;
error) echo 'Connect the rated charger first.' >&2; exit 1 ;;
normal) echo 'Normal charging selected.' ;;
esac
EOF
chmod +x "$HELPER"
nav='home 0; press start; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down'
run() {
    name=$1; result=$2; board=$3; script=$4
    : > "$OUT/calls"
    BANANA_BOARD="$board" BANANA_CMDLINE_FILE="$OUT/cmdline" BANANA_CHARGING="$HELPER" CHARGING_RESULT="$result" CHARGING_CALLS="$OUT/calls" \
    "$HERE/banana-ui" --headless "$OUT/$name-" --size 640x480 --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$OUT/fixture/roms" --data "$OUT/fixture/data" \
        --script "$nav; $script" 2> "$OUT/$name.log"
}
same() {
    [ -f "$1" ] && [ -f "$2" ] || { echo 'FAIL missing captured frame'; exit 1; }
    rc=0
    compare -metric AE "$1" "$2" null: 2> "$OUT/compare.txt" || rc=$?
    [ "$rc" -le 1 ] || { cat "$OUT/compare.txt"; echo 'FAIL image comparison'; exit 1; }
    [ "$rc" = 0 ]
}
run arm success rgds-plus 'press a; shot armed'
[ ! -s "$OUT/calls" ] || { echo 'FAIL first A invoked helper'; exit 1; }
run success success rgds-plus 'shot initial; press a; press a; shot result; press down; shot normal; press up; shot cleared'
[ "$(cat "$OUT/calls")" = 'higher --confirm-5v-3a' ] || { echo 'FAIL confirmation/arguments'; exit 1; }
same "$OUT/success-initial-top.png" "$OUT/success-cleared-top.png" || { echo 'FAIL stale feedback after navigation'; exit 1; }
run error error rgds-plus 'press a; press a; shot result; press down; shot normal'
[ "$(cat "$OUT/calls")" = 'higher --confirm-5v-3a' ] || { echo 'FAIL failed helper was not invoked'; exit 1; }
if same "$OUT/error-result-top.png" "$OUT/success-result-top.png"; then echo 'FAIL success/error feedback hidden'; exit 1; fi
same "$OUT/success-normal-top.png" "$OUT/error-normal-top.png" || { echo 'FAIL feedback leaked to another row'; exit 1; }
run normal normal rgds-plus 'press down; shot initial; press a; press a; shot result'
[ "$(cat "$OUT/calls")" = normal ] || { echo 'FAIL normal action arguments'; exit 1; }
if same "$OUT/normal-initial-top.png" "$OUT/normal-result-top.png"; then echo 'FAIL normal feedback hidden'; exit 1; fi
printf 'quiet banana.board=rgds-plus root=/dev/mmcblk0p2\n' > "$OUT/cmdline"
run universal success universal 'press a; press a; shot result'
[ "$(cat "$OUT/calls")" = 'higher --confirm-5v-3a' ] || { echo 'FAIL universal Plus detection'; exit 1; }
printf 'quiet banana.board=rgds\n' > "$OUT/cmdline"
run nonplus success universal 'shot result'
if same "$OUT/universal-result-top.png" "$OUT/nonplus-result-top.png"; then echo 'FAIL charging exposed on non-Plus'; exit 1; fi
echo 'Charging menu: confirmation, success/error feedback, normal, navigation and universal board checks passed.'
