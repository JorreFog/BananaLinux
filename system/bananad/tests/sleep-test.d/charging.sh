# Charging while off: rcS's "bananad --charge" (a charger's start, or turned off with the charger in), sourced by
# sleep-test.sh after its own scenarios (marks made with ": >": the sleep test's touch() is a touchscreen's). Only the LEDs on; the power button held starts it, the charger out turns it off.
mount -t tmpfs none /sys/devices/system/cpu     # (the made-up CPUs: the test's machine keeps its own online)
mkdir -p /sys/devices/system/cpu/cpufreq/policy0
echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
for c in 1 2 3; do mkdir -p /sys/devices/system/cpu/cpu$c; echo 1 > /sys/devices/system/cpu/cpu$c/online; done
L=/sys/class/leds; CH=/sys/class/power_supply/charger
mkdir -p $CH; echo USB > $CH/type; echo 0 > $CH/online
echo 1 > /sys/class/backlight/backlight0/bl_power; echo 0 > /sys/class/backlight/backlight0/bl_power
# charge_mode's sleep: its write of "mem" to the FIFO /sys/power/state blocking (as the main bananad's, asleep())
cb_asleep() { for i in $(seq 40); do [ "$(cat /proc/$CB/wchan 2>/dev/null)" = wait_for_partner ] && return 0; sleep 0.1; done; return 1; }
charge() { r=0; BANANAD_TEST_LID=/run/test/lid "$W/bananad" --charge || r=$?; }
# the background one's end, up to 6 s (then stopped: a FAIL, not a hung test), its exit status in r
# the background one's end, up to 15 s (the charger out: off 10 s later, as Android's charger mode), its exit status in r
finish() { for i in $(seq 150); do kill -0 $CB 2>/dev/null || break; sleep 0.1; done; kill $CB 2>/dev/null || true; r=0; wait $CB || r=$?; }

echo 0 > /run/test/lid      # (the lid open: a scenario before may have left it shut, and shut is charging 5's)
echo "charging 1. not a charger's start (no mark, no PMIC to ask): on with the boot at once; the charger's start with"
echo "   the cable out again already (in and out at once): off again, not the menu"
charge; [ $r = 1 ] && ok "no charger's start: exit 1" || bad "exit $r"
: > /data/banana/.charge-next; charge
[ $r = 3 ] && ok "turned off with the charger in, out again: exit 3 (stays off)" || bad "exit $r"
grep -q 'the charger out again already: turned off again' /data/banana/logs/charging.log && ok "logged" || bad "not logged"
[ "$(cat /sys/devices/system/cpu/cpu1/online)" = 1 ] && ok "nothing changed (the CPUs)" || bad "cpu1 offline"

echo "charging 2. a restart's mark wins over the charger: a restart isn't a charger's start"
echo 1 > $CH/online
: > /data/banana/.restart; : > /data/banana/.charge-next; charge
[ $r = 1 ] && ok "a restart: exit 1" || bad "exit $r"
[ ! -e /data/banana/.restart ] && [ ! -e /data/banana/.charge-next ] && ok "both marks used up" || bad "marks left: $(ls -a /data/banana)"

echo "charging 2b. the charger seen late (the RK817 reads it offline for the boot's first seconds): charging all the same"
echo 0 > $CH/online; : > /data/banana/.charge-next
"$W/bananad" --charge & CB=$!
sleep 2; echo 1 > $CH/online
cb_asleep && ok "charging (asleep) once the charger showed, 2 s in" || bad "not charging"
grep -q "the charger seen [0-9.]* s into charging while off" /data/banana/logs/charging.log && ok "logged how late" || bad "not logged"
ev $EV_KEY $KEY_POWER 1; wake; sleep 1.4; finish; ev $EV_KEY $KEY_POWER 0
[ $r = 0 ] && ok "held: it starts (exit 0)" || bad "exit $r"

echo "charging 3. turned off with the charger in: the panels dark, amber, asleep; a short press sleeps on, held starts it"
: > /data/banana/.charge-next
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0      # an old press, waiting: not for the charging
"$W/bananad" --charge & CB=$!
cb_asleep && ok "asleep while charging (an old press didn't start it)" || bad "not asleep"
[ -e /run/banana/charging ] && ok "/run/banana/charging while it charges" || bad "no /run/banana/charging"
[ "$(cat $L/amber:charging/brightness)" = 255 ] && [ "$(cat $L/green:power/brightness)" = 0 ] && ok "amber on at full (lit asleep), green off" ||
    bad "amber $(cat $L/amber:charging/brightness), green $(cat $L/green:power/brightness)"
[ "$(cat /sys/class/rtc/rtc0/wakealarm)" = +20 ] && ok "a look in 20 s (+20: a missed unplug still seen)" || bad "wakealarm $(cat /sys/class/rtc/rtc0/wakealarm)"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && [ "$(cat /sys/class/backlight/backlight1/bl_power)" = 4 ] && ok "both panels dark" || bad "bl_power not 4"
[ "$(cat /sys/devices/system/cpu/cpu1/online)$(cat /sys/devices/system/cpu/cpu3/online)" = 00 ] &&
    [ "$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor)" = powersave ] && ok "one CPU, powersave (for its looks)" || bad "CPUs as they were"
ev $EV_KEY $KEY_POWER 1; wake; sleep 0.3; ev $EV_KEY $KEY_POWER 0     # a short press wakes it
cb_asleep && ok "a short press: asleep again, still charging" || bad "awake after a short press"
kill -0 $CB 2>/dev/null || bad "a short press started it"
ev $EV_KEY $KEY_POWER 1; wake; sleep 1.4                              # held: it starts
finish
ev $EV_KEY $KEY_POWER 0
[ $r = 0 ] && ok "held a second: exit 0, the boot goes on" || bad "exit $r"
[ "$(cat /sys/devices/system/cpu/cpu1/online)" = 1 ] && ok "the CPUs back" || bad "cpu1 still offline"
[ ! -e /run/banana/charging ] && ok "/run/banana/charging gone" || bad "/run/banana/charging left"
grep -q "charging while off (turned off with the charger in): battery [0-9]*%" /data/banana/logs/charging.log &&
    grep -q "the power button: starting" /data/banana/logs/charging.log && ok "charging.log" || bad "charging.log: $(cat /data/banana/logs/charging.log)"

grep -q "charging while off (turned off with the charger in): battery [0-9]*%, the charging light off until now" /data/banana/logs/charging.log &&
    ok "charging.log: whether U-Boot had lit the charging light (off here)" || bad "charging.log: $(grep 'charging while off' /data/banana/logs/charging.log | tail -n1)"

echo "charging 4. full: green instead of amber at the next look; the charger pulled out turns it off"
: > /data/banana/.charge-next
"$W/bananad" --charge & CB=$!
cb_asleep && ok "asleep" || bad "not asleep"
echo 100 > /sys/class/power_supply/battery/capacity
: > /sys/class/rtc/rtc0/wakealarm; wake                               # the look: full meanwhile
cb_asleep && ok "asleep again after the look" || bad "awake after the look"
[ "$(cat $L/green:power/brightness)" = 255 ] && [ "$(cat $L/amber:charging/brightness)" = 0 ] && ok "full: green on, amber off" ||
    bad "green $(cat $L/green:power/brightness), amber $(cat $L/amber:charging/brightness)"
grep -q "full after" /data/banana/logs/charging.log && ok "charging.log: full" || bad "full not logged"
echo 0 > $CH/online; wake                                             # the cable out woke it
sleep 2.5
[ "$(cat $L/green:power/brightness)$(cat $L/amber:charging/brightness)" = 00 ] && ok "the cable out: the lights off at once" || bad "lights still on"
kill -0 $CB 2>/dev/null && ok "still on for now (off 10 s later, as Android)" || bad "off at once"
echo 1 > $CH/online; sleep 2.5                                         # back within the 10 s
kill -0 $CB 2>/dev/null && ok "the cable back within 10 s: charging on" || bad "turned off although the cable came back"
grep -q "the charger back: charging on" /data/banana/logs/charging.log && ok "charging.log: back" || bad "charging.log: $(tail -n2 /data/banana/logs/charging.log)"
[ "$(cat $L/green:power/brightness)" = 255 ] && ok "back: green again (full)" || bad "green $(cat $L/green:power/brightness)"
cb_asleep && ok "back: asleep again, charging" || bad "not asleep again"
echo 0 > $CH/online; wake                                              # out for good (its interrupt wakes it)
finish
[ $r = 3 ] && ok "the charger out 10 s: exit 3 (rcS turns it off)" || bad "exit $r"
[ "$(cat $L/green:power/brightness)$(cat $L/amber:charging/brightness)" = 00 ] && ok "the LEDs off" || bad "LEDs left on"
grep -q "the charger pulled out: turning off after [0-9]* min: battery [0-9]*% (the last wake: " /data/banana/logs/charging.log &&
    ok "charging.log: what woke it last" || bad "charging.log: $(grep 'pulled out' /data/banana/logs/charging.log | tail -n1)"
echo 50 > /sys/class/power_supply/battery/capacity

echo "charging 5. the power button with the lid shut, no charger: off again at once (lid_start = yes: it starts)"
echo 0 > $CH/online; echo 1 > /run/test/lid
charge; [ $r = 3 ] && ok "the lid shut: exit 3 (rcS turns it off)" || bad "exit $r"
grep -q "started with the lid shut: turned off again" /data/banana/logs/charging.log && ok "charging.log says so" || bad "not logged"
: > /data/banana/.restart; charge
[ $r = 1 ] && ok "a restart with the lid shut: exit 1, it goes on" || bad "exit $r"
echo "lid_start = yes" > /data/banana/bananad.conf; charge
[ $r = 1 ] && ok "lid_start = yes: exit 1, it starts" || bad "exit $r"
rm -f /data/banana/bananad.conf

echo "charging 6. the lid shut, a charger in: charging instead; held power doesn't start it until the lid opens"
echo 1 > $CH/online
BANANAD_TEST_LID=/run/test/lid "$W/bananad" --charge & CB=$!
cb_asleep && ok "asleep, charging" || bad "not asleep"
grep -q "charging while off (started with the lid shut, the charger in)" /data/banana/logs/charging.log && ok "charging.log: the lid shut, the charger in" || bad "charging.log: $(tail -n1 /data/banana/logs/charging.log)"
ev $EV_KEY $KEY_POWER 1; wake; sleep 1.4
kill -0 $CB 2>/dev/null && ok "held a second with the lid shut: still charging" || bad "it started with the lid shut"
echo 0 > /run/test/lid; sleep 1.2
finish
ev $EV_KEY $KEY_POWER 0
[ $r = 0 ] && ok "the lid opened, still held: exit 0, the boot goes on" || bad "exit $r"

echo "charging 7. the start guard (a phone's, the stock RG DS's numbers): from charging, power starts only at 6% and"
echo "   3.75 V; without a charger, under 3.6 V it turns off again; with one, too low goes to charging"
B=/sys/class/power_supply/battery
echo 4 > $B/capacity; echo 3700000 > $B/voltage_now; echo 1 > $CH/online
: > /data/banana/.charge-next
"$W/bananad" --charge & CB=$!
cb_asleep && ok "asleep, charging at 4%" || bad "not asleep"
ev $EV_KEY $KEY_POWER 1; wake; sleep 2.2; ev $EV_KEY $KEY_POWER 0
kill -0 $CB 2>/dev/null && ok "held at 4%, 3.70 V: not started, still charging" || bad "it started too low"
grep -q "the power button: the battery too low to start (4%, 3700 mV), charging on until 6% and 3.75 V" /data/banana/logs/charging.log &&
    ok "charging.log: too low to start" || bad "charging.log: $(tail -n1 /data/banana/logs/charging.log)"
echo 6 > $B/capacity; echo 3800000 > $B/voltage_now
sleep 0.3; ev $EV_KEY $KEY_POWER 1; wake; sleep 1.4
finish
ev $EV_KEY $KEY_POWER 0
[ $r = 0 ] && ok "held at 6%, 3.80 V: exit 0, it starts" || bad "exit $r"
echo 0 > $CH/online; echo 2 > $B/capacity; echo 3550000 > $B/voltage_now
charge; [ $r = 3 ] && ok "pressed at 3.55 V without a charger: exit 3, off again" || bad "exit $r"
grep -q "the battery too low to start (3550 mV): turned off again" /data/banana/logs/charging.log &&
    ok "charging.log: turned off again" || bad "charging.log: $(tail -n1 /data/banana/logs/charging.log)"
echo 3650000 > $B/voltage_now
charge; [ $r = 1 ] && ok "at 3.65 V without a charger: exit 1, it starts (2% is the user's to use)" || bad "exit $r"
echo 1 > $CH/online; echo 3700000 > $B/voltage_now
"$W/bananad" --charge & CB=$!
cb_asleep && ok "pressed at 2% with a charger in: charging while off instead" || bad "not charging"
grep -q "charging while off (the battery too low to start, the charger in)" /data/banana/logs/charging.log &&
    ok "charging.log: too low, the charger in" || bad "charging.log: $(tail -n1 /data/banana/logs/charging.log)"
echo 0 > $CH/online; : > /sys/class/rtc/rtc0/wakealarm; wake
finish
[ $r = 3 ] && ok "the charger out: exit 3" || bad "exit $r"
echo "charging 8. slow pre-sleep steps (the Wi-Fi's, a minute on the device once) don't hold up the cable or the power button"
mkdir -p /etc/banana/sleep.d && mount -t tmpfs none /etc/banana/sleep.d
printf '#!/bin/sh\n[ "$1" = pre ] && sleep 6\nexit 0\n' > /etc/banana/sleep.d/50-slow; chmod +x /etc/banana/sleep.d/50-slow
echo 1 > $CH/online; echo 50 > $B/capacity; echo 4000000 > $B/voltage_now   # (enough to start: the guard passes)
: > /data/banana/.charge-next
"$W/bananad" --charge & CB=$!
sleep 0.5
[ "$(cat $L/amber:charging/brightness)" = 255 ] && ok "amber at once, the steps still running" || bad "amber $(cat $L/amber:charging/brightness)"
echo 0 > $CH/online                                                  # pulled out during the steps
sleep 2.5
[ "$(cat $L/amber:charging/brightness)" = 0 ] && ok "the cable out during the steps: the light off within 2.5 s" || bad "the light still on"
echo 1 > $CH/online; sleep 2.5                                         # back
ev $EV_KEY $KEY_POWER 1; sleep 1.5; ev $EV_KEY $KEY_POWER 0           # held 1.5 s, the steps still running
finish
[ $r = 0 ] && ok "held power started it during the steps: exit 0" || bad "exit $r"
tail -n1 /data/banana/logs/charging.log | grep -q "the power button: starting" && ok "charging.log: started" || bad "charging.log: $(tail -n1 /data/banana/logs/charging.log)"
sleep 6                                                                # (the steps end, then the post ones, in the background)
umount /etc/banana/sleep.d

echo 50 > $B/capacity; rm -f $B/voltage_now
rm -rf $CH
umount /sys/devices/system/cpu

