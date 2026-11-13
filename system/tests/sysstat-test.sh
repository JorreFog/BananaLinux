#!/bin/sh
# usr/libexec/banana/sysstat against a made-up /proc and /sys: the CPU's share between two calls, the fastest core,
# the hottest zone of each kind, the GPU's clock, memory, the battery's power with its sign, the fan; a kernel that
# gives none of it prints nothing and fails nothing.
HERE=$(cd "$(dirname "$0")/.." && pwd)
X=$HERE/rootfs/usr/libexec/banana/sysstat
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0; bad() { echo "FAIL sysstat: $*"; fail=1; }
export BANANA_SYS="$T/sys" BANANA_PROC="$T/proc" BANANA_RUN="$T/run"
mkdir -p "$T/proc" "$T/empty"
[ -z "$(BANANA_SYS="$T/empty" BANANA_PROC="$T/empty" sh "$X")" ] || bad "values from nothing: $(BANANA_SYS="$T/empty" BANANA_PROC="$T/empty" sh "$X" | tr '\n' ' ')"
s=$T/sys
mkdir -p $s/devices/system/cpu/cpufreq/policy0 $s/devices/system/cpu/cpufreq/policy7 $s/class/thermal/thermal_zone0 $s/class/thermal/thermal_zone1 \
    $s/class/thermal/thermal_zone2 $s/class/devfreq/3d00000.gpu $s/class/power_supply/battery $s/class/power_supply/usb $s/class/hwmon/hwmon3
echo 1804800 > $s/devices/system/cpu/cpufreq/policy0/scaling_cur_freq; echo 2710000 > $s/devices/system/cpu/cpufreq/policy7/scaling_cur_freq
echo cpu-0-0 > $s/class/thermal/thermal_zone0/type; echo 47000 > $s/class/thermal/thermal_zone0/temp
echo cpu-1-2 > $s/class/thermal/thermal_zone1/type; echo 50400 > $s/class/thermal/thermal_zone1/temp
echo gpu-top > $s/class/thermal/thermal_zone2/type; echo 44000 > $s/class/thermal/thermal_zone2/temp
echo 680000000 > $s/class/devfreq/3d00000.gpu/cur_freq
echo Battery > $s/class/power_supply/battery/type; echo Charging > $s/class/power_supply/battery/status; echo 100 > $s/class/power_supply/battery/capacity
echo 800000 > $s/class/power_supply/battery/current_now; echo 4125000 > $s/class/power_supply/battery/voltage_now
echo USB > $s/class/power_supply/usb/type; echo 41 > $s/class/hwmon/hwmon3/pwm1
printf 'MemTotal:        7340032 kB\nMemFree:          100 kB\nMemAvailable:    5242880 kB\n' > "$T/proc/meminfo"
echo "cpu  1000 0 500 8000 500 0 0 0 0 0" > "$T/proc/stat"
o=$(sh "$X"); echo "$o" | grep -q '^cpu=' && bad "a CPU share from one sample"
echo "cpu  1150 0 550 8750 550 0 0 0 0 0" > "$T/proc/stat"
o=$(sh "$X")
for want in cpu=20 cpu_mhz=2710 cpu_c=50 gpu_c=44 gpu_mhz=680 mem_used_mb=2048 mem_total_mb=7168 mem_pct=28 power_state=Charging battery=100 power_mw=3300 fan_pct=16; do
    echo "$o" | grep -qx "$want" || bad "no $want in: $(echo "$o" | tr '\n' ' ')"
done
echo "$o" | grep -q '^gpu=' && bad "a GPU load with no file for one"
# out of the battery: negative whichever sign the driver gives
echo Discharging > $s/class/power_supply/battery/status
sh "$X" | grep -qx 'power_mw=-3300' || bad "discharging: $(sh "$X" | grep power)"
[ $fail = 0 ] && echo "sysstat: all checks passed"
exit $fail
