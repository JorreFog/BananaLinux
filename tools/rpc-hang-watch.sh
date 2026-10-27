#!/bin/sh
# Keep the kernel's words on the card so a hang that ends at the power button still leaves a trace.
#
#   sh rpc-hang-watch.sh start [seconds]      default 900 s
#   sh rpc-hang-watch.sh stop
#   sh rpc-hang-watch.sh show                 the newest capture: the ring buffer and the samples at the end
#
# The ring buffer is copied whole, once a second, to one of two files and `sync`ed. The file that is not being
# written is the one that survives a power cut, and it is at most a second behind. A log that lives in RAM
# (/run/banana/messages) or in the kernel's own buffer is gone when the handheld is switched off, which is exactly
# the case this exists for.
#
# Sampled along with it: the hottest sensor, the fan's PWM and its tachometer, the GPU and CPU clocks, and the
# load -- so a thermal shutdown, a clock that never came up, and a bus lockup can be told apart.
D=/data/banana/logs
P=$D/hang-watch.pid

case "$1" in
stop)
	[ -f $P ] && kill "$(cat $P)" 2>/dev/null
	rm -f $P
	echo "hang-watch: stopped"
	exit 0
	;;
show)
	n=$(ls -t $D/hang-watch.0.txt $D/hang-watch.1.txt 2>/dev/null | head -n 1)
	[ -n "$n" ] || { echo "no capture in $D"; exit 1; }
	echo "=== $n: the kernel buffer as of the last second before the handheld died ==="
	tail -n 50 "$n"
	s=$D/hang-watch.stats.${n##*.hang-watch.}
	echo "=== $s: the last samples (epoch hot= mC fan=pwm/rpm gpu=Hz load) ==="
	tail -n 15 "$s" 2>/dev/null
	exit 0
	;;
esac

secs=${2:-900}
mkdir -p $D

# every kernel message into the buffer, and the sysrq keys alive, so a half-dead system can still be asked for a
# task list before the power button goes
echo 8 > /proc/sys/kernel/printk 2>/dev/null
echo 1 > /proc/sys/kernel/sysrq 2>/dev/null

(
	n=0
	end=$(($(date +%s) + secs))
	while [ "$(date +%s)" -lt "$end" ]; do
		n=$((1 - n))
		dmesg > "$D/hang-watch.$n.txt" 2>/dev/null
		{
			printf "%s" "$(date +%s)"
			printf " hot=%s" "$(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | sort -n | tail -n 1)"
			printf " fan=%s/%s" "$(cat /sys/class/hwmon/hwmon*/pwm1 2>/dev/null | head -n 1)" "$(cat /sys/class/hwmon/hwmon*/fan1_input 2>/dev/null | head -n 1)"
			printf " gpu=%s" "$(cat /sys/class/devfreq/*/cur_freq 2>/dev/null | head -n 1)"
			printf " cpu=%s" "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null)"
			printf " load=%s" "$(cut -d " " -f 1-3 /proc/loadavg 2>/dev/null)"
			printf "\n"
		} >> "$D/hang-watch.stats.$n.txt" 2>/dev/null
		sync
		sleep 1
	done
) &
echo $! > $P
echo "hang-watch: capturing to $D/hang-watch.*.txt for $secs s (pid $(cat $P))"
