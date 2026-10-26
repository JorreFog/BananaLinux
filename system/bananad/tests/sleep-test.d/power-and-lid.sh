# sleep-test.sh scenarios: the power button, shutdowns, the lid, the charger, a low battery, SIGHUP
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)

echo "power 1. the power button: a press sleeps, and the press that woke it doesn't send it back to sleep"
echo $GAME > /run/banana/game.pid
start
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
asleep && ok "asleep after a press" || bad "no sleep on a press"
wake
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0     # inside the second after the wake: the same press, bouncing
asleep && bad "back to sleep straight after the wake" || ok "a press just after the wake is ignored"
[ "$(gstate)" != T ] && ok "game running" || bad "game still stopped"
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0     # later: a real press again
asleep && ok "a later press sleeps again" || bad "no sleep on a later press"
wake
grep -q 'sleep (power button)' /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
stop

echo "power 2. the power button held: a clean shutdown, the game told to quit, no sleep on the release"
start "power_long_ms = 400"
ev $EV_KEY $KEY_POWER 1
for i in $(seq 30); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
[ $POWEROFF = 1 ] && ok "shut down (init asked to power off)" || bad "no shutdown on a held press"
ev $EV_KEY $KEY_POWER 0
asleep 2>/dev/null && bad "it slept on the release" || ok "no sleep on the release"
kill -0 $GAME 2>/dev/null && bad "the game wasn't told to quit" || ok "the game was told to quit"
stop
POWEROFF=0

echo "power 3. a shutdown waits for a game that takes a moment to write its saves"
rm -f /run/test/saved
setsid sh -c 'trap "sleep 1; touch /run/test/saved; exit 0" TERM; while :; do sleep 0.1; done' 2>/dev/null & GAME=$!
sleep 0.2; echo $GAME > /run/banana/game.pid
trap 'POWEROFF=1; [ -f /run/test/saved ] && SAVED=1' USR2
SAVED=0
start "power_long_ms = 300"
ev $EV_KEY $KEY_POWER 1
for i in $(seq 70); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
ev $EV_KEY $KEY_POWER 0
[ $POWEROFF = 1 ] && ok "shut down" || bad "no shutdown"
[ $SAVED = 1 ] && ok "the game's saves were written before the power-off" || bad "powered off before the game had saved"
stop
trap 'POWEROFF=1' USR2
POWEROFF=0

echo "power 4. a game that ignores SIGTERM doesn't hold the shutdown up"
setsid sh -c 'trap "" TERM; while :; do sleep 0.1; done' 2>/dev/null & GAME=$!
sleep 0.2; echo $GAME > /run/banana/game.pid
start "power_long_ms = 300"
t0=$(date +%s)
ev $EV_KEY $KEY_POWER 1
for i in $(seq 90); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
ev $EV_KEY $KEY_POWER 0
[ $POWEROFF = 1 ] && ok "shut down anyway, after $(( $(date +%s) - t0 )) s" || bad "no shutdown with a game that won't quit"
stop
kill -9 -$GAME 2>/dev/null || true
POWEROFF=0
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid

echo "power 5. the lid shut and opened in a flick: one short sleep, then awake for good"
start
ev $EV_SW $SW_LID 1; ev $EV_SW $SW_LID 0       # (the lid's file says open: it was only a moment)
asleep && ok "slept on the close" || bad "no sleep on the close"
wake
asleep && bad "back to sleep after the flick" || ok "awake after the wake, the open event not taken again"
[ "$(gstate)" != T ] && ok "game running" || bad "game still stopped"
stop

echo "power 6. a charger plugged in while asleep with the lid shut: awake and dark, not asleep (the charging light stays on)"
start
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "asleep" || bad "no sleep"
mkdir -p /sys/class/power_supply/charger
echo USB > /sys/class/power_supply/charger/type; echo 1 > /sys/class/power_supply/charger/online
wake      # the plug woke it; the lid is still shut
asleep 2>/dev/null && bad "back to sleep on the charger" || ok "awake on the charger"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && ok "panels dark" || bad "backlight bl_power $(cat /sys/class/backlight/backlight0/bl_power)"
a=$(cat /sys/class/leds/amber:charging/brightness)
[ -n "$a" ] && [ "$a" != 0 ] && ok "charging light on ($a)" || bad "charging light off ('$a')"
[ "$(gstate)" = T ] && ok "game still paused" || bad "game running with the lid shut ($(gstate))"
echo 0 > /run/test/lid; sleep 1
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 0 ] && ok "panels back when the lid opens" || bad "panels still dark"
[ "$(gstate)" != T ] && ok "game running again" || bad "game still paused"
rm -rf /sys/class/power_supply/charger
stop

echo "power 7. one low battery reading while awake isn't a shutdown (a load spike); it takes two in a row"
echo 2 > /sys/class/power_supply/battery/capacity
start "battery_shutdown_percent = 3"
sleep 2
[ $POWEROFF = 0 ] && ok "no shutdown on the first low reading" || bad "shut down on one reading"
stop
echo 50 > /sys/class/power_supply/battery/capacity
POWEROFF=0

echo "power 8. bananad.conf read again on SIGHUP (Settings changed lid sleep): the lid then only darkens the panels"
start "lid_sleep = yes"
printf '%s\n' "autosleep_minutes = 0" "sleep_check_hours = 0" "lid_sleep = no" > /data/banana/bananad.conf
kill -HUP $BD; sleep 0.3
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1; sleep 0.3
asleep 2>/dev/null && bad "it slept: the new setting wasn't read" || ok "no sleep after the reload"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && ok "panels dark" || bad "panels on"
echo 0 > /run/test/lid; ev $EV_SW $SW_LID 0; sleep 0.3
stop
rm -f /run/banana/game.pid
