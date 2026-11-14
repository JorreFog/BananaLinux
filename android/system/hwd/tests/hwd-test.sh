#!/bin/sh
# banana-hwd against made-up /sys, /proc and /dev trees (no handheld, no Android): builds the daemon and its client
# with the host's compiler, then checks the protocol (PROTOCOL.md) on two kernels' worth of nodes:
#   vendor    AYN's Qualcomm 5.15 kernel as LineageOS runs it: fan trips bound to a pwm-fan cooling device,
#             left:stick:N lights (R G B), panel0/panel1-backlight, kgsl, walt
#   mainline  ROCKNIX's device tree: a pwm-fan hwmon nothing else drives, rgb:l1-r4 lights (B G R), the DSI
#             backlights, devfreq 3d00000.gpu with a 1 GHz clock, schedutil
# and the Home button through a pipe standing in for the input device (tap, then a hold).
#   sh android/system/hwd/tests/hwd-test.sh        (CC=clang to use another compiler; KEEP=1 keeps the trees)
set -u
HERE=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/hwd-test.XXXXXX")
CC=${CC:-cc}
PASS=0
FAIL=0
DPID=
cleanup() { [ -n "$DPID" ] && kill "$DPID" 2>/dev/null; wait 2>/dev/null; [ -n "${KEEP:-}" ] || rm -rf "$T"; }
trap cleanup EXIT

ok() { PASS=$((PASS + 1)); echo "ok   $1"; }
no() { FAIL=$((FAIL + 1)); echo "FAIL $1"; [ -n "${2:-}" ] && echo "     got: $2"; }
# check <name> <expected substring> <command words...>
check() {
    n=$1; want=$2; shift 2
    got=$("$T/banana-hwc" -s "$SOCK" "$@" 2>&1)
    case $got in *"$want"*) ok "$n" ;; *) no "$n" "$got" ;; esac
}
# file <name> <path> <expected content>
file_is() {
    got=$(cat "$2" 2>/dev/null)
    [ "$got" = "$3" ] && ok "$1" || no "$1" "$got (wanted $3)"
}
put() { mkdir -p "$(dirname "$1")"; printf '%s\n' "$2" > "$1"; }

F="-std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -Wno-format-truncation"
$CC $F -o "$T/banana-hwd" "$HERE/banana-hwd.c" "$HERE/hw.c" || { echo "FAIL build"; exit 1; }
$CC $F -o "$T/banana-hwc" "$HERE/banana-hwc.c" || { echo "FAIL build"; exit 1; }
ok "builds with $CC -Werror"

common_tree() {   # common_tree <root>: CPUs, memory, battery
    R=$1
    printf 'cpu  1000 0 1000 8000 0 0 0 0 0 0\n' > "$R/proc/stat"
    printf 'MemTotal:        8000000 kB\nMemFree:  100 kB\nMemAvailable:    6000000 kB\n' > "$R/proc/meminfo"
    put "$R/proc/sys/kernel/random/boot_id" "boot-1"
    for p in 0 3 7; do
        d=$R/sys/devices/system/cpu/cpufreq/policy$p
        put "$d/cpuinfo_min_freq" 300000
        put "$d/cpuinfo_max_freq" 2000000
        put "$d/scaling_available_frequencies" "300000 600000 1000000 1400000 1800000 2000000"
        put "$d/scaling_min_freq" 300000
        put "$d/scaling_max_freq" 2000000
        put "$d/scaling_cur_freq" $((900000 + p * 100000))
    done
    b=$R/sys/class/power_supply/battery
    put "$b/type" Battery; put "$b/status" Discharging; put "$b/capacity" 15
    put "$b/current_now" 500000; put "$b/voltage_now" 4000000
    put "$R/sys/class/power_supply/usb/type" USB
}

vendor_tree() {
    R=$T/vendor; mkdir -p "$R/proc" "$R/sys" "$R/dev/input" "$R/data"
    common_tree "$R"
    for p in 0 3 7; do
        d=$R/sys/devices/system/cpu/cpufreq/policy$p
        put "$d/scaling_available_governors" "walt performance"; put "$d/scaling_governor" walt
    done
    th=$R/sys/class/thermal
    put "$th/cooling_device35/type" pwm-fan; put "$th/cooling_device35/cur_state" 2; put "$th/cooling_device35/max_state" 8
    put "$th/cooling_device3/type" thermal-cpufreq-0
    # three zones bound to the fan, one bound elsewhere
    z=$th/thermal_zone1; put "$z/type" cpu-0-0-0; put "$z/temp" 52000
    put "$z/trip_point_2_temp" 50000; put "$z/trip_point_3_temp" 55000
    mkdir -p "$z/cdev0" "$z/cdev1"; put "$z/cdev0/type" pwm-fan; put "$z/cdev0_trip_point" 2
    put "$z/cdev1/type" pwm-fan; put "$z/cdev1_trip_point" 3
    z=$th/thermal_zone2; put "$z/type" gpuss-0; put "$z/temp" 48000; put "$z/trip_point_3_temp" 70000
    mkdir -p "$z/cdev0"; put "$z/cdev0/type" pwm-fan; put "$z/cdev0_trip_point" 3
    z=$th/thermal_zone9; put "$z/type" usb-therm; put "$z/temp" 41000
    put "$z/trip_point_2_temp" 43000; put "$z/trip_point_3_temp" 45000
    mkdir -p "$z/cdev0" "$z/cdev1"; put "$z/cdev0/type" pwm-fan; put "$z/cdev0_trip_point" 2
    put "$z/cdev1/type" pwm-fan; put "$z/cdev1_trip_point" 3
    z=$th/thermal_zone4; put "$z/type" xo-therm; put "$z/temp" 39000; put "$z/trip_point_0_temp" 90000
    mkdir -p "$z/cdev0"; put "$z/cdev0/type" thermal-cpufreq-0; put "$z/cdev0_trip_point" 0
    put "$R/sys/class/hwmon/hwmon0/pwm1" 60
    for s in left right; do for n in 0 1 2 3; do
        put "$R/sys/class/leds/$s:stick:$n/multi_intensity" "0 0 0"; put "$R/sys/class/leds/$s:stick:$n/brightness" 0
    done; done
    for p in 0 1; do
        put "$R/sys/class/backlight/panel$p-backlight/brightness" 100
        put "$R/sys/class/backlight/panel$p-backlight/max_brightness" 4095
    done
    k=$R/sys/class/kgsl/kgsl-3d0
    put "$k/gpu_busy_percentage" "37 %"
    put "$k/devfreq/cur_freq" 680000000; put "$k/devfreq/governor" msm-adreno-tz
    put "$k/devfreq/available_governors" "msm-adreno-tz performance powersave"
    put "$k/devfreq/available_frequencies" "719000000 680000000 220000000"; put "$k/devfreq/max_freq" 719000000
    mkdir -p "$R/sys/class/devfreq/soc:qcom,gpubw"   # (must not be taken for the GPU)
    mkfifo "$R/dev/input/event0"
}

mainline_tree() {
    R=$T/mainline; mkdir -p "$R/proc" "$R/sys" "$R/dev/input" "$R/data"
    common_tree "$R"
    for p in 0 3 7; do
        d=$R/sys/devices/system/cpu/cpufreq/policy$p
        put "$d/scaling_available_governors" "schedutil performance"; put "$d/scaling_governor" schedutil
    done
    th=$R/sys/class/thermal
    put "$th/thermal_zone0/type" cpu0-top-thermal; put "$th/thermal_zone0/temp" 72000
    put "$th/thermal_zone1/type" gpuss0-thermal; put "$th/thermal_zone1/temp" 68000
    put "$th/thermal_zone2/type" pm8550-thermal; put "$th/thermal_zone2/temp" 30000
    put "$R/sys/class/hwmon/hwmon2/name" pwmfan; put "$R/sys/class/hwmon/hwmon2/pwm1" 0
    put "$R/sys/class/hwmon/hwmon2/pwm1_enable" 0
    for s in l r; do for n in 1 2 3 4; do
        put "$R/sys/class/leds/rgb:$s$n/multi_intensity" "0 0 0"; put "$R/sys/class/leds/rgb:$s$n/brightness" 0
    done; done
    put "$R/sys/class/backlight/ae96000.dsi.0/brightness" 10; put "$R/sys/class/backlight/ae96000.dsi.0/max_brightness" 255
    g=$R/sys/class/devfreq/3d00000.gpu
    put "$g/cur_freq" 680000000; put "$g/governor" simple_ondemand
    put "$g/available_governors" "simple_ondemand performance powersave"
    put "$g/available_frequencies" "220000000 680000000 1000000000"; put "$g/max_freq" 680000000
}

start() {   # start <root> <conf>
    SOCK=$1/hwd.sock
    BANANA_SYS=$1/sys BANANA_PROC=$1/proc BANANA_DEV=$1/dev BANANA_HWD_STATE=$1/data/hwd.state \
        "$T/banana-hwd" -c "$2" -s "$SOCK" 2>"$1/hwd.log" &
    DPID=$!
    i=0; while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
    [ -S "$SOCK" ] && ok "starts ($(basename "$1"))" || { no "starts"; cat "$1/hwd.log"; exit 1; }
}
stop() { kill "$DPID"; wait "$DPID" 2>/dev/null; DPID=; rm -f "$SOCK"; }

# ---------------------------------------------------------------- vendor kernel (LineageOS)
vendor_tree
R=$T/vendor
printf '# test\nhome_devices =\nhome_hold_ms = 300\n' > "$R/hwd.conf"
start "$R" "$R/hwd.conf"
check "ping" "OK pong 1" PING
check "caps" "fan fan_trips leds perf backlight_top backlight_bottom temps home" CAPS
# the boot default, quiet: the CPU/GPU ladder as at boot, usb-therm's 43/45 C moved to 60/62 C
file_is "quiet: usb-therm first trip 60 C" "$R/sys/class/thermal/thermal_zone9/trip_point_2_temp" 60000
file_is "quiet: usb-therm keeps its gap" "$R/sys/class/thermal/thermal_zone9/trip_point_3_temp" 62000
file_is "quiet: cpu ladder untouched" "$R/sys/class/thermal/thermal_zone1/trip_point_2_temp" 50000
check "fan mode aggressive" "OK" FAN MODE aggressive
file_is "aggressive: cpu trip 10 C sooner" "$R/sys/class/thermal/thermal_zone1/trip_point_2_temp" 40000
file_is "aggressive: cpu trip 2 10 C sooner" "$R/sys/class/thermal/thermal_zone1/trip_point_3_temp" 45000
file_is "aggressive: gpu trip 10 C sooner" "$R/sys/class/thermal/thermal_zone2/trip_point_3_temp" 60000
file_is "a trip not bound to the fan is never touched" "$R/sys/class/thermal/thermal_zone4/trip_point_0_temp" 90000
file_is "pwm1 never written on this kernel" "$R/sys/class/hwmon/hwmon0/pwm1" 60
check "fan get" "mode=aggressive backend=trips pwm=60 pct=23 state=2 max_state=8 trips=5" FAN
check "fan trips listed" "usb-therm:trip_point_2_temp=60000/43000" FAN TRIPS
check "no fan off on this kernel" "ERR unsupported" FAN MODE off
check "no fixed speed on this kernel" "ERR unsupported" FAN SET 40
check "bad fan mode" "ERR badarg" FAN MODE loud
check "fan mode stock" "OK" FAN MODE stock
file_is "stock: usb-therm back" "$R/sys/class/thermal/thermal_zone9/trip_point_2_temp" 43000
file_is "stock: cpu back" "$R/sys/class/thermal/thermal_zone1/trip_point_2_temp" 50000
check "fan mode max" "OK" FAN MODE max
file_is "max: floored at 40 C" "$R/sys/class/thermal/thermal_zone2/trip_point_3_temp" 40000
# lights: AYN's names, R G B
check "leds color" "OK" LEDS COLOR ff8000
file_is "leds: R G B on left:stick:0" "$R/sys/class/leds/left:stick:0/multi_intensity" "255 128 0"
file_is "leds: right:stick:3 too" "$R/sys/class/leds/right:stick:3/multi_intensity" "255 128 0"
file_is "leds: medium" "$R/sys/class/leds/left:stick:2/brightness" 140
check "leds level high" "OK" LEDS LEVEL high
file_is "leds: high" "$R/sys/class/leds/left:stick:2/brightness" 255
check "leds system colour" "OK" LEDS MODE system
check "game start" "OK" GAME START psx performance
file_is "leds: PlayStation cyan" "$R/sys/class/leds/right:stick:1/multi_intensity" "0 220 255"
check "leds battery" "OK" LEDS MODE battery
file_is "leds: battery under 20 % is red" "$R/sys/class/leds/left:stick:1/multi_intensity" "255 0 0"
check "leds get" "mode=battery color=ff8000 level=255 effect=none system=psx rings=8" LEDS
check "bad colour" "ERR badarg" LEDS COLOR nope
# clocks: walt kept (no schedutil on this kernel), performance where asked
file_is "perf: performance governor" "$R/sys/devices/system/cpu/cpufreq/policy7/scaling_governor" performance
file_is "perf: gpu performance" "$R/sys/class/kgsl/kgsl-3d0/devfreq/governor" performance
check "perf powersave" "OK" PERF SET powersave
file_is "powersave: walt kept" "$R/sys/devices/system/cpu/cpufreq/policy0/scaling_governor" walt
file_is "powersave: 71 % cap" "$R/sys/devices/system/cpu/cpufreq/policy3/scaling_max_freq" 1400000
file_is "powersave: gpu back to its own" "$R/sys/class/kgsl/kgsl-3d0/devfreq/governor" msm-adreno-tz
check "perf steady" "OK" PERF SET steady
file_is "steady: 55 % floor" "$R/sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq" 1000000
check "game end" "OK" GAME END
check "perf get" "profile=ui" PERF
check "no gpu boost here" "ERR unsupported" GPUBOOST on
# panels
check "backlight top" "OK 100 4095" BACKLIGHT top
check "backlight bottom set" "OK" BACKLIGHT bottom 2000
file_is "bottom is panel1" "$R/sys/class/backlight/panel1-backlight/brightness" 2000
check "backlight out of range" "ERR failed" BACKLIGHT top 5000
# statistics
"$T/banana-hwc" -s "$SOCK" STATS >/dev/null
printf 'cpu  1500 0 1500 8500 0 0 0 0 0 0\n' > "$R/proc/stat"
check "stats" "OK cpu=67 cpu_mhz=1600 cpu_c=52 gpu_c=48 gpu_mhz=680 gpu=37 mem_used_mb=1953 mem_total_mb=7812 mem_pct=25 power_state=Discharging battery=15 power_mw=-2000 fan_pct=23" STATS
# sleep: lights off, back after
check "suspend" "OK" SUSPEND
file_is "asleep: lights off" "$R/sys/class/leds/left:stick:0/brightness" 0
check "resume" "OK" RESUME
file_is "awake: lights back" "$R/sys/class/leds/left:stick:0/brightness" 255
check "unknown command" "ERR unknown" FROB
# the Home button: a tap, then a hold of 0.6 s (hold_ms 300 here): down, tap, down, hold, release
"$T/banana-hwc" -s "$SOCK" -e 6 -t 4000 > "$R/events" 2>&1 &
EPID=$!
sleep 0.3
python3 -I -c '
import struct, sys, time
def ev(f, v):
    f.write(struct.pack("llHHi", 0, 0, 1, 194, v)); f.write(struct.pack("llHHi", 0, 0, 0, 0, 0)); f.flush()
with open(sys.argv[1], "wb", buffering=0) as f:
    ev(f, 1); time.sleep(0.1); ev(f, 0); time.sleep(0.2)
    ev(f, 1); time.sleep(0.2); ev(f, 2); time.sleep(0.4); ev(f, 0); time.sleep(0.2)
' "$R/dev/input/event0"
wait $EPID
want="subscribed
EVENT home down
EVENT home tap
EVENT home down
EVENT home hold
EVENT home release"
[ "$(cat "$R/events")" = "$want" ] && ok "home: tap, then hold mid-press and release" || no "home events" "$(cat "$R/events" | tr '\n' '|')"
stop
grep -q '^fan_mode=max$' "$R/data/hwd.state" && grep -q '^leds_mode=battery$' "$R/data/hwd.state" && ok "settings kept" || no "settings kept" "$(cat "$R/data/hwd.state")"
grep -q 'thermal_zone9/trip_point_2_temp 43000' "$R/data/hwd.state.trips" && ok "boot trips recorded" || no "boot trips recorded"
# a restart in the same boot: the moved trips are not taken for the boot values; the mode comes back
start "$R" "$R/hwd.conf"
check "restart keeps boot trips" "cpu-0-0-0:trip_point_2_temp=40000/50000" FAN TRIPS
file_is "restart: max back on" "$R/sys/class/thermal/thermal_zone1/trip_point_2_temp" 40000
stop

# ---------------------------------------------------------------- mainline (ROCKNIX's device tree)
mainline_tree
R=$T/mainline
printf 'fan_interval_ms = 200\n' > "$R/hwd.conf"
start "$R" "$R/hwd.conf"
check "caps" "fan fan_pwm leds perf gpuboost backlight_top temps home" CAPS
sleep 0.4
# quiet's curve at the 70 C mean of cpu0-top and gpuss0: > base+10 (60+10) is false, > base+5 is true -> 77
file_is "pwm curve: quiet at 70 C" "$R/sys/class/hwmon/hwmon2/pwm1" 77
file_is "pwm enabled" "$R/sys/class/hwmon/hwmon2/pwm1_enable" 1
check "fan aggressive" "OK" FAN MODE aggressive
file_is "pwm curve: aggressive at 70 C (> base+15)" "$R/sys/class/hwmon/hwmon2/pwm1" 119
check "fan manual" "OK" FAN SET 40
file_is "manual 40 %" "$R/sys/class/hwmon/hwmon2/pwm1" 102
check "leds color" "OK" LEDS COLOR ff8000
file_is "leds: B G R on rgb:l1" "$R/sys/class/leds/rgb:l1/multi_intensity" "0 128 255"
check "gpu boost" "OK" GPUBOOST on
file_is "gpu 1 GHz" "$R/sys/class/devfreq/3d00000.gpu/max_freq" 1000000000
check "perf balanced" "OK" PERF SET balanced
file_is "balanced: schedutil" "$R/sys/devices/system/cpu/cpufreq/policy7/scaling_governor" schedutil
file_is "balanced: gpu's own governor" "$R/sys/class/devfreq/3d00000.gpu/governor" simple_ondemand
check "suspend" "OK" SUSPEND
file_is "asleep: fan stopped" "$R/sys/class/hwmon/hwmon2/pwm1" 0
check "backlight top" "OK 10 255" BACKLIGHT top
check "no bottom backlight node" "ERR unsupported" BACKLIGHT bottom
stop

echo "$PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
