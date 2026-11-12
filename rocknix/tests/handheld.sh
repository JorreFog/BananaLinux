#!/bin/sh
# usr/libexec/banana/handheld against a made-up /sys (the AYN Thor's fan and its two HTR3212 rings) and stand-ins for
# ROCKNIX's tools: what this handheld has, the fan profile as ROCKNIX's cooling.profile with its service restarted,
# the stick lights in a colour, by system, off, the battery's, the breathing effect, the GPU boost.
HERE=$(cd "$(dirname "$0")/.." && pwd)
H=$HERE/../system/rootfs/usr/libexec/banana/handheld
T=$(mktemp -d)
trap '[ -f "$T/run/leds-effect.pid" ] && kill $(cat "$T/run/leds-effect.pid") 2>/dev/null; rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL handheld: $*"; fail=1; }
mkdir -p "$T/sys/class/hwmon/hwmon3" "$T/tools" "$T/data" "$T/run"
: > "$T/sys/class/hwmon/hwmon3/pwm1"
for s in l r; do for n in 1 2 3 4; do d=$T/sys/devices/platform/multi-led$s$n/leds/rgb:$s$n; mkdir -p "$d"; echo 0 > "$d/brightness"; done; done
for t in analog_sticks_ledcontrol ledcontrol gpu_overclock systemctl; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/log"\n' "$t" "$T" > "$T/tools/$t"; chmod +x "$T/tools/$t"
done
export BANANA_SYS="$T/sys" BANANA_TOOLS="$T/tools" BANANA_DATA="$T/data" BANANA_RUN="$T/run" ROCKNIX_SETTINGS="$T/system.cfg" \
    BANANA_SYSTEMCTL="$T/tools/systemctl" BANANA_LIBEXEC="$(dirname "$H")"
ok() { grep -qF -- "$2" "$T/$1" || bad "no '$2' in $1"; }
[ "$(sh "$H" caps | tr '\n' ' ')" = "fan leds gpuboost " ] || bad "caps: $(sh "$H" caps | tr '\n' ' ')"
sh "$H" fan aggressive; ok system.cfg "cooling.profile=aggressive"; ok log "systemctl restart fancontrol"
sh "$H" fan loud 2>/dev/null && bad "an unknown fan profile was taken"
sh "$H" leds blue high; ok log "analog_sticks_ledcontrol 255 0 0 255 0 0 255"; ok system.cfg "led.color=rgb"
printf 'leds = system\nleds_level = low\n' > "$T/data/settings.conf"
sh "$H" leds-system psx; ok log "analog_sticks_ledcontrol 64 0 220 255 0 220 255"
sh "$H" leds off; ok log "ledcontrol off"; ok system.cfg "led.color=off"
sh "$H" leds battery; ok log "ledcontrol battery"
printf 'leds = red\nleds_effect = breathe\n' > "$T/data/settings.conf"
sh "$H" leds red; sleep 0.5
[ -f "$T/run/leds-effect.pid" ] || bad "no breathing effect running"
[ "$(cat "$T/sys/devices/platform/multi-ledl1/leds/rgb:l1/brightness")" != 0 ] || bad "the effect didn't touch the rings"
sh "$H" leds-effect none; [ ! -f "$T/run/leds-effect.pid" ] || bad "the effect didn't stop"
sh "$H" gpu on; ok log "gpu_overclock enable"
sh "$H" poweroff-after 60; ok system.cfg "system.shutdown_delay=3600"; ok system.cfg "system.suspend.enable_timed_shutdown=1"
sh "$H" poweroff-after 0; ok system.cfg "system.suspend.enable_timed_shutdown=0"
# a handheld with none of it: nothing listed, nothing done
[ -z "$(BANANA_SYS="$T/none" BANANA_TOOLS="$T/none" sh "$H" caps)" ] || bad "caps on a plain handheld"
# BananaOS's own image (none of ROCKNIX's tools): the same commands done here. The fan's speed from the CPU zones'
# temperature by the profile's curve, the rings' files written (blue, green, red), the GPU's top clock, the battery's colour
N=$T/native; mkdir -p "$N/sys/class/hwmon/hwmon1" "$N/sys/class/thermal/thermal_zone0" "$N/sys/class/thermal/thermal_zone1" \
    "$N/sys/class/devfreq/3d00000.gpu" "$N/sys/class/power_supply/battery" "$N/tools" "$N/run" "$N/data"
echo 0 > "$N/sys/class/hwmon/hwmon1/pwm1"; echo 0 > "$N/sys/class/hwmon/hwmon1/pwm1_enable"
echo cpu-0-0 > "$N/sys/class/thermal/thermal_zone0/type"; echo 70000 > "$N/sys/class/thermal/thermal_zone0/temp"
echo battery > "$N/sys/class/thermal/thermal_zone1/type"; echo 20000 > "$N/sys/class/thermal/thermal_zone1/temp"
echo 680000000 > "$N/sys/class/devfreq/3d00000.gpu/max_freq"
echo 12 > "$N/sys/class/power_supply/battery/capacity"; echo Discharging > "$N/sys/class/power_supply/battery/status"
for s in l r; do for n in 1 2 3 4; do d=$N/sys/devices/platform/multi-led$s$n/leds/rgb:$s$n; mkdir -p "$d"; echo 0 > "$d/brightness"; : > "$d/multi_intensity"; done; done
nat() { env BANANA_SYS="$N/sys" BANANA_TOOLS="$N/tools" BANANA_DATA="$N/data" BANANA_RUN="$N/run" BANANA_FAN_INTERVAL=0.2 ROCKNIX_SETTINGS= sh "$H" "$@"; }
L1="$N/sys/devices/platform/multi-ledl1/leds/rgb:l1"
[ "$(nat caps | tr '\n' ' ')" = "fan leds gpuboost " ] || bad "native caps: $(nat caps | tr '\n' ' ')"
nat fan moderate; sleep 0.6
[ "$(cat "$N/sys/class/hwmon/hwmon1/pwm1")" = 102 ] || bad "native fan at 70 degrees, moderate: $(cat "$N/sys/class/hwmon/hwmon1/pwm1")"
echo 40000 > "$N/sys/class/thermal/thermal_zone0/temp"; sleep 0.6
[ "$(cat "$N/sys/class/hwmon/hwmon1/pwm1")" = 0 ] || bad "native fan at 40 degrees: $(cat "$N/sys/class/hwmon/hwmon1/pwm1")"
kill "$(cat "$N/run/fan.pid")" 2>/dev/null
nat leds blue high
[ "$(cat "$L1/multi_intensity")" = "255 0 0" ] && [ "$(cat "$L1/brightness")" = 255 ] || bad "native leds blue: $(cat "$L1/multi_intensity") $(cat "$L1/brightness")"
nat leds battery; sleep 0.3
[ "$(cat "$L1/multi_intensity")" = "0 0 255" ] || bad "native battery colour at 12 %: $(cat "$L1/multi_intensity")"
nat leds off; [ "$(cat "$L1/brightness")" = 0 ] || bad "native leds off"
[ -f "$N/run/leds-battery.pid" ] && bad "the battery's loop outlived leds off"
nat gpu on; [ "$(cat "$N/sys/class/devfreq/3d00000.gpu/max_freq")" = 1000000000 ] || bad "native gpu boost"
# a sleep: the rings dark and every loop gone (the fan's, the battery colour's, the breathing), the fan stopped; then
# boot's restore brings the fan's loop and the rings back as Settings keeps them
printf 'fan = moderate\nleds = blue\nleds_effect = breathe\n' > "$N/data/settings.conf"
echo 70000 > "$N/sys/class/thermal/thermal_zone0/temp"
nat boot; sleep 0.6
[ -f "$N/run/fan.pid" ] && [ -f "$N/run/leds-effect.pid" ] || bad "native boot: the fan's loop and the breathing"
nat sleep
[ -f "$N/run/fan.pid" ] || [ -f "$N/run/leds-effect.pid" ] || [ -f "$N/run/leds-battery.pid" ] && bad "a loop outlived the sleep"
sleep 0.3
[ "$(cat "$L1/brightness")" = 0 ] && [ "$(cat "$N/sys/class/hwmon/hwmon1/pwm1")" = 0 ] || bad "asleep: rings $(cat "$L1/brightness"), fan $(cat "$N/sys/class/hwmon/hwmon1/pwm1")"
nat boot; sleep 0.6
[ "$(cat "$N/sys/class/hwmon/hwmon1/pwm1")" = 102 ] && [ "$(cat "$L1/multi_intensity")" = "255 0 0" ] || bad "after the sleep: fan $(cat "$N/sys/class/hwmon/hwmon1/pwm1"), rings $(cat "$L1/multi_intensity")"
for l in fan leds-effect leds-battery; do [ -f "$N/run/$l.pid" ] && kill "$(cat "$N/run/$l.pid")" 2>/dev/null; done
# on ROCKNIX its own sleep hooks do it: nothing here
n=$(wc -l < "$T/log"); sh "$H" sleep; [ "$(wc -l < "$T/log")" = "$n" ] || bad "sleep under ROCKNIX called its tools"
[ $fail = 0 ] && echo "handheld: all passed"
exit $fail
