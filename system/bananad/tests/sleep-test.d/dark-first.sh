# sleep-test.sh scenario: the panels go dark as a sleep begins, not once the pre-sleep hooks are done
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)
# (the hooks take Wi-Fi and Bluetooth down, ~2 s on the RG DS Plus: the panels stayed lit that long with the lid shut)

bl() { echo "$(cat /sys/class/backlight/backlight0/bl_power)$(cat /sys/class/backlight/backlight1/bl_power)"; }
printf '#!/bin/sh\n[ "$1" = pre ] && sleep 1.5\nexit 0\n' > /etc/banana/sleep.d/05-slow; chmod +x /etc/banana/sleep.d/05-slow

echo "dark-first 1. the lid shut: both panels dark while the pre-sleep hooks still run, lit again when it opens"
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
sleep 0.4
[ "$(bl)" = 44 ] && ok "both panels dark during the hooks" || bad "bl_power $(bl) during the hooks"
asleep && ok "asleep after the hooks" || bad "no sleep"
echo 0 > /run/test/lid; wake
[ "$(bl)" = 00 ] && ok "both panels lit after the wake, the lid open" || bad "bl_power $(bl) after the wake"
stop

echo "dark-first 2. the power button, the lid open: dark during the hooks, lit after the wake"
start
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
sleep 0.4
[ "$(bl)" = 44 ] && ok "both panels dark during the hooks" || bad "bl_power $(bl) during the hooks"
asleep && ok "asleep after the hooks" || bad "no sleep"
wake
[ "$(bl)" = 00 ] && ok "both panels lit after the wake" || bad "bl_power $(bl) after the wake"
stop

rm -f /etc/banana/sleep.d/05-slow
