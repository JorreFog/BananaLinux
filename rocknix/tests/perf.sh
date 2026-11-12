#!/bin/sh
# banana-perf on a handheld with clusters (a made-up cpufreq tree with the AYN Thor's three policies and an Adreno
# devfreq): each profile's governor and clocks per cluster, the GPU's governor.
HERE=$(cd "$(dirname "$0")/.." && pwd)
BP=$HERE/../system/rootfs/usr/bin/banana-perf
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL perf: $*"; fail=1; }
# policy <n> <available kHz...>
policy() {
    d=$T/cpufreq/policy$1; shift; mkdir -p "$d"
    echo "$*" > "$d/scaling_available_frequencies"
    echo "$1" > "$d/cpuinfo_min_freq"; eval echo \${$#} > "$d/cpuinfo_max_freq"
    for f in scaling_min_freq scaling_max_freq scaling_governor; do : > "$d/$f"; done
}
policy 0 307200 1094400 1497600 2016000
policy 3 499200 1209600 1900800 2803200
policy 7 595200 1440000 2208000 3187200
mkdir -p "$T/devfreq/3d00000.gpu"; : > "$T/devfreq/3d00000.gpu/governor"
run() { env BANANA_CPUFREQ="$T/cpufreq" BANANA_DEVFREQ="$T/devfreq" sh "$BP" "$1"; }
v() { cat "$T/cpufreq/policy$1/$2"; }
run performance
for p in 0 3 7; do [ "$(v $p scaling_governor)" = performance ] || bad "performance: policy$p's governor $(v $p scaling_governor)"; done
[ "$(v 7 scaling_max_freq)" = 3187200 ] || bad "performance: policy7's max"
[ "$(cat "$T/devfreq/3d00000.gpu/governor")" = performance ] || bad "performance: the GPU"
run powersave
# 71% of 3187200 = 2262912 -> the nearest at or under it: 2208000; policy0 71% of 2016000 -> 1497600? no: 1431360 -> 1094400
[ "$(v 7 scaling_max_freq)" = 2208000 ] || bad "powersave: policy7's max $(v 7 scaling_max_freq)"
[ "$(v 0 scaling_max_freq)" = 1094400 ] || bad "powersave: policy0's max $(v 0 scaling_max_freq)"
[ "$(v 3 scaling_governor)" = schedutil ] || bad "powersave: the governor"
[ "$(cat "$T/devfreq/3d00000.gpu/governor")" = simple_ondemand ] || bad "powersave: the GPU"
run steady
# 55% floor: policy3 55% of 2803200 = 1541760 -> 1209600; the max its top
[ "$(v 3 scaling_min_freq)" = 1209600 ] && [ "$(v 3 scaling_max_freq)" = 2803200 ] || bad "steady: policy3 $(v 3 scaling_min_freq)-$(v 3 scaling_max_freq)"
run ui
[ "$(v 0 scaling_min_freq)" = 307200 ] || bad "ui: the floor back down"
[ "$(cat "$T/devfreq/3d00000.gpu/governor")" = powersave ] || bad "ui: the GPU"
run nonsense 2>/dev/null && bad "an unknown profile was taken"
[ $fail = 0 ] && echo "perf: all passed"
exit $fail
