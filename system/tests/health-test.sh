#!/bin/sh
# The boot's hardware check (/usr/libexec/banana/health) against a made-up handheld: /proc/bus/input/devices with
# the bitmaps the kernel writes, the panels' connectors, backlights, LEDs, sound cards and a battery in /sys.
#   sh system/tests/health-test.sh
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
H=$HERE/rootfs/usr/libexec/banana/health
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { pass=$((pass + 1)); echo "  ok: $*"; }
bad() { fail=$((fail + 1)); echo "  FAIL: $*"; }

# dev <name> <TYPE=bit,bit...>...: an input device as /proc/bus/input/devices lists it (64-bit words, highest first)
dev() {
    python3 -I - "$@" <<'PY'
import sys
name, specs = sys.argv[1], sys.argv[2:]
print('I: Bus=0019 Vendor=0001 Product=0001 Version=0100')
print('N: Name="%s"' % name)
for spec in specs:
    t, bits = spec.split('=')
    v = 0
    for b in bits.split(','): v |= 1 << int(b)
    words = []
    while v: words.append(v & (2**64 - 1)); v >>= 64
    print('B: %s=%s' % (t, ' '.join('%x' % w for w in reversed(words)) or '0'))
print()
PY
}
# handheld <board>: the whole RG DS (Plus), everything there
handheld() {
    R=$T/$1; rm -rf "$R"
    mkdir -p "$R/etc/banana" "$R/proc/bus/input" "$R/proc/asound" "$R/proc/device-tree" "$R/run/banana" "$R/data/banana/logs" \
        "$R/sys/class/backlight/backlight0" "$R/sys/class/backlight/backlight1" "$R/sys/class/net" "$R/dev/dri" \
        "$R/sys/class/power_supply/battery" "$R/sys/class/drm/card0-DSI-1" "$R/sys/class/drm/card0-DSI-2" "$R/sys/class/drm/card0-Writeback-1"
    echo "$1" > "$R/etc/banana/board"
    echo "rockchip,rk3568" > "$R/proc/device-tree/compatible"
    echo "4.20 1.00" > "$R/proc/uptime"
    : > "$R/proc/cmdline"
    { dev rk817_pwrkey KEY=116; dev gpio-keys-volume KEY=114,115; dev retrogame_joypad KEY=304,305,307,308 ABS=0,1,3,4
      dev gpio-keys-lid SW=0; dev rk817_jack SW=2; dev Goodix-TS ABS=0,1,47,53,54,57 KEY=330; } > "$R/proc/bus/input/devices"
    for c in DSI-1 DSI-2; do echo connected > "$R/sys/class/drm/card0-$c/status"; done
    echo disconnected > "$R/sys/class/drm/card0-Writeback-1/status"
    echo Battery > "$R/sys/class/power_supply/battery/type"; echo 77 > "$R/sys/class/power_supply/battery/capacity"
    for l in green:power red:status amber:charging; do mkdir -p "$R/sys/class/leds/$l"; done
    printf ' 0 [rk817hp        ]: simple-card - rk817_hp\n' > "$R/proc/asound/cards"
    [ "$1" = rgds-plus ] && printf ' 1 [aw88166        ]: aw88166 - aw88166\n' >> "$R/proc/asound/cards"
    : > "$R/run/banana/bananad-ready"; mkdir -p "$R/sys/class/net/wlan0"; : > "$R/dev/dri/renderD128"
}
run() { HEALTH_ROOT=$R HEALTH_EARLY_WAIT=${EW:-0.3} HEALTH_LATE_WAIT=${LW:-0.5} sh "$H" "$@"; }
line() { grep -E "^$1 +$2" "$R/run/banana/health" >/dev/null; }

echo "1. an RG DS Plus with everything there"
handheld rgds-plus
run early; run late
for p in "power button" "volume buttons" "gamepad buttons" "lid switch" "both screens" "screen backlights" sound LEDs \
    "battery gauge" "data partition" "buttons and sound service" touchscreen Wi-Fi GPU; do
    line OK "$p" && ok "OK: $p" || bad "$p: $(grep "$p" "$R/run/banana/health")"
done
[ ! -e "$R/run/banana/health-notice" ] && ok "no notice" || bad "a notice: $(cat "$R/run/banana/health-notice")"
[ -s "$R/data/banana/logs/health.txt" ] && ok "the report on the card" || bad "no report on the card"

echo "2. the touchscreen probing late (its driver is asynchronous): LATE, no notice"
handheld rgds-plus
sed -i.bak '/Goodix/,/^$/d' "$R/proc/bus/input/devices"
run early
( sleep 0.3; dev Goodix-TS ABS=0,1,53,54 >> "$R/proc/bus/input/devices" ) &
LW=3 run late; wait
line LATE touchscreen && ok "LATE: touchscreen" || bad "touchscreen: $(grep touchscreen "$R/run/banana/health")"
[ ! -e "$R/run/banana/health-notice" ] && ok "no notice" || bad "a notice: $(cat "$R/run/banana/health-notice")"

echo "3. no touchscreen and no Wi-Fi: MISSING, and the menu's notice names both"
handheld rgds-plus
sed -i.bak '/Goodix/,/^$/d' "$R/proc/bus/input/devices"; rm -rf "$R/sys/class/net/wlan0"
run early; run late
line MISSING touchscreen && line MISSING Wi-Fi && ok "MISSING: touchscreen, Wi-Fi" || bad "$(grep MISSING "$R/run/banana/health")"
[ "$(cat "$R/run/banana/health-notice" 2>/dev/null)" = "touchscreen, Wi-Fi" ] && ok "notice: $(cat "$R/run/banana/health-notice")" || bad "notice: '$(cat "$R/run/banana/health-notice" 2>/dev/null)'"

echo "4. an input device with keys but not the power key: the power button MISSING (the bitmaps read right)"
handheld rgds-plus
sed -i.bak '/rk817_pwrkey/,/^$/d' "$R/proc/bus/input/devices"; dev rk817_pwrkey KEY=112 >> "$R/proc/bus/input/devices"   # 116 -> 112
run early
line MISSING "power button" && ok "MISSING: power button" || bad "$(grep 'power button' "$R/run/banana/health")"
grep -q "power button" "$R/run/banana/health-notice" && ok "in the notice" || bad "not in the notice"

echo "5. the RG DS Plus without its speaker amplifiers: sound MISSING; a handheld without its red LED: LEDs, log only"
handheld rgds-plus; sed -i.bak '/aw88166/d' "$R/proc/asound/cards"; run early
line MISSING sound && grep -q sound "$R/run/banana/health-notice" && ok "sound MISSING, in the notice" || bad "$(grep sound "$R/run/banana/health")"
handheld rgds; rm -rf "$R/sys/class/leds/red:status"; run early
line MISSING LEDs && ok "LEDs MISSING on the RG DS" || bad "$(grep LEDs "$R/run/banana/health")"
[ ! -e "$R/run/banana/health-notice" ] && ok "LEDs aren't in the notice" || bad "notice: $(cat "$R/run/banana/health-notice")"

echo "6. a part that turns up while the early check waits: LATE"
handheld rgds-plus; rm -f "$R/run/banana/bananad-ready"
( sleep 0.3; : > "$R/run/banana/bananad-ready" ) &
EW=2 run early; wait
line LATE "buttons and sound service" && ok "LATE: buttons and sound service" || bad "$(grep service "$R/run/banana/health")"

echo "7. QEMU's virtual handheld: its own list, everything found, said on the console"
R=$T/qemu; rm -rf "$R"; mkdir -p "$R/proc/bus/input" "$R/proc/device-tree" "$R/run/banana" "$R/data/banana/logs" "$R/dev" \
    "$R/sys/class/drm/card0-Virtual-1" "$R/sys/class/drm/card0-Virtual-2" "$R/etc/banana"
echo rgds-plus > "$R/etc/banana/board"; printf 'linux,dummy-virt\0' > "$R/proc/device-tree/compatible"
echo "6.00 1.00" > "$R/proc/uptime"; echo "console=ttyAMA0 banana.console=1" > "$R/proc/cmdline"; : > "$R/dev/console"
for c in Virtual-1 Virtual-2; do echo connected > "$R/sys/class/drm/card0-$c/status"; done
dev "QEMU Virtio Keyboard" KEY=1,28,103,108 > "$R/proc/bus/input/devices"; : > "$R/run/banana/bananad-ready"
run early; run late
grep -q "health: everything found (qemu)" "$R/dev/console" && ok "console: $(cat "$R/dev/console")" || bad "console: $(cat "$R/dev/console")"
[ "$(grep -c '^OK' "$R/run/banana/health")" = 3 ] && ok "3 parts checked" || bad "$(cat "$R/run/banana/health")"

echo "8. the kernel's own input list, not a plain file: BusyBox's read polls, and this file only polls ready when a"
echo "   device comes or goes (a read straight from it hung the boot); the check must come back within its waits"
if [ -e /proc/bus/input/devices ]; then
    handheld rgds-plus
    rm -f "$R/proc/bus/input/devices"; ln -s /proc/bus/input/devices "$R/proc/bus/input/devices"
    if timeout 15 env HEALTH_ROOT=$R HEALTH_EARLY_WAIT=0.3 sh "$H" early; then ok "came back with the kernel's input list"
    else bad "hung (or failed) on the kernel's /proc/bus/input/devices"; fi
else
    echo "  (skipped: no /proc/bus/input/devices here)"
fi

echo "9. a panel reading \"unknown\" early in the boot counts; a missing log-only part isn't waited for"
handheld rgds-plus; echo unknown > "$R/sys/class/drm/card0-DSI-2/status"; rm -rf "$R/sys/class/leds"
t0=$(date +%s); EW=3 run early; t1=$(date +%s)
line OK "both screens" && ok "OK: both screens (one reading unknown)" || bad "$(grep screens "$R/run/banana/health")"
line MISSING LEDs && [ $((t1 - t0)) -lt 2 ] && ok "LEDs MISSING without waiting the 3 s" || bad "LEDs waited for ($((t1 - t0)) s): $(grep LEDs "$R/run/banana/health")"
[ ! -e "$R/run/banana/health-notice" ] && ok "no notice for log-only parts" || bad "notice: $(cat "$R/run/banana/health-notice")"

echo "health test: $pass passed, $fail failed"
[ $fail = 0 ]
