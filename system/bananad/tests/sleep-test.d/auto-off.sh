# sleep-test.sh scenarios: turning off when idle or asleep long enough, the LED and the panels while asleep
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, ok and bad)

echo "auto-off 1. asleep long enough (poweroff_minutes): off cleanly, the game saving first"
echo $GAME > /run/banana/game.pid
start "poweroff_minutes = 0.02"     # 1.2 s asleep
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "asleep" || bad "no sleep"
[ "$(cat /sys/class/rtc/rtc0/wakealarm)" = +2 ] && ok "the RTC set for turning off (+2 s), not the battery check" || bad "wakealarm $(cat /sys/class/rtc/rtc0/wakealarm)"
sleep 1.5; : > /sys/class/rtc/rtc0/wakealarm; wake
for i in $(seq 30); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
[ $POWEROFF = 1 ] && ok "turned off (init asked to power off)" || bad "not turned off"
kill -0 $GAME 2>/dev/null && bad "the game wasn't told to quit and save" || ok "the game was told to quit and save first"
grep -q 'turning off' /data/banana/logs/sleep.log && ok "logged: $(grep 'turning off' /data/banana/logs/sleep.log | tail -n1 | cut -c21-)" || bad "not logged"
grep -q "woke after .*: the RTC's alarm" /data/banana/logs/sleep.log && ok "logged the alarm's wake" || bad "the alarm's wake not logged"
# the lid still shut: it mustn't sleep again while rcK shuts down (it did, and froze the shutdown until the next wake)
for i in $(seq 70); do [ -e /run/test/wake.asleep ] && break; sleep 0.1; done
[ ! -e /run/test/wake.asleep ] && ok "no sleep again during the shutdown (7 s, the lid still shut)" || bad "slept again in the middle of the shutdown"
stop
POWEROFF=0; echo 0 > /run/test/lid
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid

echo "auto-off 2. on the charger at the turn-off time: it sleeps on (the RG DS Plus doesn't charge switched off)"
start "poweroff_minutes = 0.02"
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
asleep && ok "asleep (the power button)" || bad "no sleep"
mkdir -p /sys/class/power_supply/charger
echo USB > /sys/class/power_supply/charger/type; echo 1 > /sys/class/power_supply/charger/online
sleep 1.5; : > /sys/class/rtc/rtc0/wakealarm; wake
[ $POWEROFF = 0 ] && ok "not turned off on the charger" || bad "turned off on the charger"
asleep && ok "asleep again" || bad "awake after the alarm"
wake      # a press: awake
rm -rf /sys/class/power_supply/charger
stop
POWEROFF=0

echo "auto-off 3. sleep at never: off after the idle time awake; in a game only with the timers everywhere"
rm -f /run/banana/game.pid
start "poweroff_minutes = 0.02"     # (start's autosleep_minutes = 0)
sleep 2.5
[ $POWEROFF = 1 ] && ok "off after the idle time in the menu" || bad "not turned off when idle"
grep -q 'idle .* min: turning off' /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
stop; POWEROFF=0
echo $GAME > /run/banana/game.pid
start "poweroff_minutes = 0.02"
sleep 2.5
[ $POWEROFF = 0 ] && ok "not during a game (the timers in the menu)" || bad "turned off during a game"
stop
start "poweroff_minutes = 0.02" "idle_in_games = yes"
for i in $(seq 30); do [ $POWEROFF = 1 ] && break; sleep 0.1; done
[ $POWEROFF = 1 ] && ok "off during an idle game with the timers everywhere" || bad "not turned off during a game"
kill -0 $GAME 2>/dev/null && bad "the game wasn't told to quit and save" || ok "the game was told to quit and save first"
stop; POWEROFF=0
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid

echo "auto-off 4. the timers everywhere: a touch is input in a game (the stylus alone isn't idle)"
echo 200 > /sys/class/backlight/backlight0/brightness; echo 200 > /sys/class/backlight/backlight1/brightness
start "idle_in_games = yes" "dim_minutes = 0.03"     # 1.8 s
for i in 1 2 3 4 5 6 7; do touch; sleep 0.5; done
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 200 ] && ok "touching kept the panels bright (3.5 s)" || bad "dimmed while touching: $(cat /sys/class/backlight/backlight1/brightness)"
sleep 2.5
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 60 ] && ok "dimmed once the touching stopped" || bad "not dimmed: $(cat /sys/class/backlight/backlight1/brightness)"
touch; sleep 1.5
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 200 ] && ok "a touch brought them back" || bad "still dim after a touch: $(cat /sys/class/backlight/backlight1/brightness)"
stop

echo "auto-off 5. the timers everywhere: an idle game sleeps, paused, and carries on after the wake"
start "idle_in_games = yes" "autosleep_minutes = 0.02"
asleep && ok "asleep in a game" || bad "no sleep in a game"
[ "$(gstate)" = T ] && ok "the game paused" || bad "the game ran on ($(gstate))"
wake
[ "$(gstate)" != T ] && ok "the game carries on after the wake" || bad "the game still paused"
stop
kill -CONT -$GAME 2>/dev/null || true
rm -f /run/banana/game.pid

echo "auto-off 6. the upper LED lit while asleep (led_sleep on: full, the kernel keeps it), back at its level after; off: dark"
G=/sys/class/leds/green:power/brightness
start
[ "$(cat $G)" = 128 ] && ok "awake: 128" || bad "awake: $(cat $G)"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "asleep" || bad "no sleep"
[ "$(cat $G)" = 255 ] && ok "asleep: lit (255)" || bad "asleep: $(cat $G)"
echo 0 > /run/test/lid; wake
[ "$(cat $G)" = 128 ] && ok "back to 128 after the wake" || bad "after the wake: $(cat $G)"
stop
start "led_sleep = off"
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "asleep" || bad "no sleep"
[ "$(cat $G)" = 0 ] && ok "asleep with led_sleep off: dark" || bad "asleep: $(cat $G)"
echo 0 > /run/test/lid; wake
stop

echo "auto-off 7. the panels dark as soon as it goes to sleep (not when the kernel gets round to it), lit again after"
echo 0 > /sys/class/backlight/backlight0/bl_power
start
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
asleep && ok "asleep" || bad "no sleep"
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 4 ] && ok "panels dark while asleep" || bad "panels lit while asleep ($(cat /sys/class/backlight/backlight0/bl_power))"
wake
[ "$(cat /sys/class/backlight/backlight0/bl_power)" = 0 ] && ok "panels lit after the wake" || bad "panels dark after the wake"
stop

echo "auto-off 8. a power press after the turn-off time, the alarm not having woken it: a wake, not a shutdown"
start "poweroff_minutes = 0.02" "autosleep_minutes = 30"     # 1.2 s, the alarm +2 s (sleep set: not the awake turn-off)
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
asleep && ok "asleep (the power button)" || bad "no sleep"
sleep 3                             # past the alarm's time; wakealarm left set: it didn't fire
wake                                # the power button
[ $POWEROFF = 0 ] && ok "not turned off: someone woke it" || bad "turned off on a power press"
grep -q "not the RTC's alarm" /data/banana/logs/sleep.log && ok "logged: $(grep 'woke after' /data/banana/logs/sleep.log | tail -n1 | cut -c21-)" || bad "the wake's cause not logged"
stop
POWEROFF=0
