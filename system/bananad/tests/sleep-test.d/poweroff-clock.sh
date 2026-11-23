# sleep-test.sh scenario: Power off after sleeping when the wall clock's second ticks over between the deadline's
# check and the RTC alarm (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok, bad)

echo "poweroff-clock 1. a second that ticks over before the alarm is set: the alarm is still set for the deadline"
# (half-second-clock.so: the first reading of time() is the deadline's start, the second its check, the third,
# a second later, the alarm's. Read again for the alarm, the deadline was 0 s away: no alarm at all, and the
# handheld slept on past the deadline and the battery check until someone woke it)
rm -f /run/banana/game.pid
export LD_PRELOAD="$W/half-second-clock.so"
start "poweroff_after_sleep_minutes = 0.02"      # 1 s
unset LD_PRELOAD
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
if asleep; then
    ok "suspended"
    [ "$(cat /sys/class/rtc/rtc0/wakealarm)" = +1 ] && ok "the RTC alarm at the deadline (+1)" || bad "wakealarm '$(cat /sys/class/rtc/rtc0/wakealarm)' (0: no alarm)"
    sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
else
    bad "no suspend"
fi
for i in $(seq 50); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
[ $POWEROFF = 1 ] && ok "shut down at the deadline" || bad "no shutdown"
stop; POWEROFF=0; echo 0 > /run/test/lid; echo 0 > /sys/class/backlight/backlight0/bl_power
