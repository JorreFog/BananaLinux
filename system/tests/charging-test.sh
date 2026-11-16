#!/bin/sh
# The real command against a private sysfs-shaped directory. No device writes.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TOOL=$HERE/system/rootfs/usr/libexec/banana/charging
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT HUP INT TERM
export BANANA_POWER_SUPPLY_ROOT=$T BANANA_BOARD=rgds-plus
passed=0
fixture() {
    rm -rf "$T/charger" "$T/battery"
    mkdir "$T/charger" "$T/battery"
    printf 'USB\n' > "$T/charger/type"
    printf '1\n' > "$T/charger/online"
    printf '1500000\n' > "$T/charger/input_current_limit"
    printf '2000000\n' > "$T/battery/constant_charge_current_max"
    printf '4350000\n' > "$T/battery/constant_charge_voltage_max"
}
run() {
    want=$1; shift
    rc=0
    sh "$TOOL" "$@" > "$T/out" 2>&1 || rc=$?
    [ "$rc" = "$want" ] || { cat "$T/out"; echo "FAIL expected exit $want, got $rc: $*"; exit 1; }
    passed=$((passed + 1))
}
limit() {
    actual=$(cat "$T/charger/input_current_limit")
    [ "$actual" = "$1" ] || { echo "FAIL unexpected limit: $actual"; exit 1; }
}
fixture
run 0 status; limit 1500000
run 2 higher; limit 1500000
run 2 higher --unconfirmed; limit 1500000
run 2 higher --confirm-5v-3a extra; limit 1500000
run 2 unknown; limit 1500000
run 2 normal extra; limit 1500000
run 0 higher --confirm-5v-3a; limit 2000000
run 0 higher --confirm-5v-3a; limit 2000000
run 0 normal; limit 1500000
run 0 normal; limit 1500000
before=$(sha256sum "$T/battery/"*)
run 0 higher --confirm-5v-3a
[ "$(sha256sum "$T/battery/"*)" = "$before" ] || { echo 'FAIL battery limits changed'; exit 1; }

fixture
BANANA_BOARD=rgds run 1 higher --confirm-5v-3a; limit 1500000
BANANA_BOARD=unknown run 1 higher --confirm-5v-3a; limit 1500000
printf '0\n' > "$T/charger/online"
run 1 higher --confirm-5v-3a; limit 1500000
# A sentinel proves disconnected normal makes no write, unlike a plain file's
# permissive write semantics (the actual kernel would return ENODEV).
printf '2000000\n' > "$T/charger/input_current_limit"
run 0 normal; limit 2000000
rm "$T/charger/online"
run 1 higher --confirm-5v-3a; limit 2000000

fixture
printf 'quiet banana.board=rgds-plus root=/dev/mmcblk0p2\n' > "$T/cmdline"
BANANA_BOARD=universal BANANA_CMDLINE_FILE="$T/cmdline" run 0 higher --confirm-5v-3a; limit 2000000
fixture
printf 'quiet banana.board=rgds\n' > "$T/cmdline"
BANANA_BOARD=universal BANANA_CMDLINE_FILE="$T/cmdline" run 1 higher --confirm-5v-3a; limit 1500000
printf 'quiet\n' > "$T/cmdline"
BANANA_BOARD=universal BANANA_CMDLINE_FILE="$T/cmdline" run 1 higher --confirm-5v-3a; limit 1500000
BANANA_BOARD=universal BANANA_CMDLINE_FILE="$T/missing" run 1 higher --confirm-5v-3a; limit 1500000

for property in constant_charge_current_max constant_charge_voltage_max; do
    for value in '' nonsense -1 0 999999999999999999999999999999999; do
        fixture
        printf '%s\n' "$value" > "$T/battery/$property"
        run 1 higher --confirm-5v-3a; limit 1500000
    done
    fixture; rm "$T/battery/$property"
    run 1 higher --confirm-5v-3a; limit 1500000
done
fixture
printf '3500000\n' > "$T/battery/constant_charge_current_max"
run 1 higher --confirm-5v-3a; limit 1500000
fixture
printf '4400000\n' > "$T/battery/constant_charge_voltage_max"
run 1 higher --confirm-5v-3a; limit 1500000
fixture
printf 'Battery\n' > "$T/charger/type"
run 1 higher --confirm-5v-3a; limit 1500000
fixture
rm "$T/charger/input_current_limit"
run 1 higher --confirm-5v-3a
[ ! -e "$T/charger/input_current_limit" ] || { echo 'FAIL created unsupported attribute'; exit 1; }
fixture
printf 'invalid\n' > "$T/charger/input_current_limit"
run 1 higher --confirm-5v-3a; limit invalid
fixture
chmod a-w "$T/charger/input_current_limit"
if [ ! -w "$T/charger/input_current_limit" ]; then
    run 1 higher --confirm-5v-3a; limit 1500000
fi
printf 'charging command: %s cases passed (private files, not a kernel/device test).\n' "$passed"
