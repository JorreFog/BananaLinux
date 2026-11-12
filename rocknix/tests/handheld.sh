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
# a handheld with none of it: nothing listed, nothing done
[ -z "$(BANANA_SYS="$T/none" BANANA_TOOLS="$T/none" sh "$H" caps)" ] || bad "caps on a plain handheld"
[ $fail = 0 ] && echo "handheld: all passed"
exit $fail
