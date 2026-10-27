# sleep-test.sh scenarios: bananad's start: the buttons live at once, a LED that turns up late
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)

echo "start-up 1. the buttons heard at once at start, while the sound is still being set up (a slow alsactl restore)"
mkdir -p "$W/bin"; printf '#!/bin/sh\nsleep 3\n' > "$W/bin/alsactl"; chmod +x "$W/bin/alsactl"
: > /data/banana/asound.state; : > /run/banana/boot-times
rm -f /run/banana/game.pid
P0=$PATH; PATH="$W/bin:$PATH"; start; PATH=$P0
[ ! -e /run/banana/bananad-ready ] && ok "not ready yet: the sound still being set up" || bad "ready before the sound was set up"
grep -q 'bananad: buttons live' /run/banana/boot-times && ok "boot-times: buttons live" || bad "no 'buttons live' in boot-times"
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
# within a second (a bananad still in its setup would take the press only after alsactl's 3 s). Asleep: the test's
# wake marker where sleep-test.sh has one, else bananad blocked in its write of "mem" on the FIFO
s=1; for i in $(seq 10); do
    { [ -e /run/test/wake.asleep ] || [ "$(cat /proc/$BD/wchan 2>/dev/null)" = wait_for_partner ]; } && { s=0; break; }; sleep 0.1
done
[ $s = 0 ] && ok "the power button heard within a second, during the sound's setup (a sleep)" || bad "the power button not heard during the sound's setup"
wake
for i in $(seq 40); do [ -e /run/banana/bananad-ready ] && break; sleep 0.1; done
[ -e /run/banana/bananad-ready ] && ok "ready once the sound was set up" || bad "never ready"
grep -q 'bananad: sound ready' /run/banana/boot-times && ok "boot-times: $(grep 'bananad' /run/banana/boot-times | tr -s ' ' | tr '\n' ';')" || bad "no 'sound ready' in boot-times"
stop
rm -f /data/banana/asound.state

echo "start-up 2. a LED that turns up after bananad started (its driver probing late) is used once it's there"
A=/sys/class/leds/amber:charging
rm -rf $A
start
mkdir -p $A; : > $A/trigger; : > $A/brightness
mkdir -p /sys/class/power_supply/charger
echo USB > /sys/class/power_supply/charger/type; echo 1 > /sys/class/power_supply/charger/online
kill -HUP $BD; sleep 0.5      # (settings read again: the LEDs set again)
a=$(cat $A/brightness)
[ -n "$a" ] && [ "$a" != 0 ] && ok "the late charging LED lit on the charger ($a)" || bad "the late charging LED not used ('$a')"
rm -rf /sys/class/power_supply/charger
stop
