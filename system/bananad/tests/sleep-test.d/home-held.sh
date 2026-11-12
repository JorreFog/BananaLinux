# sleep-test.sh scenario: a game held behind the Home button's menu (banana-ui's /run/banana/game.held beside game.pid)
# (sourced by sleep-test.sh, after its own: its helpers start, stop, ev, wake, asleep, poweroff, ok and bad)
# (the menu is what shows then: it dims and sleeps on idle as the menu does, a press in it is game.held's time, since
# the menu holds the buttons for itself, and the game is left stopped at a wake and let go for a shutdown)

setsid sleep 600 & HELD=$!; sleep 0.1
hstate() { awk '{print $3}' /proc/$HELD/stat 2>/dev/null; }
echo $HELD > /run/banana/game.pid; echo $HELD > /run/banana/game.held
echo 200 > /sys/class/backlight/backlight0/brightness; echo 200 > /sys/class/backlight/backlight1/brightness

echo "home-held 1. the menu over a held game dims on idle, and is lit again by a press the menu reports (game.held's time)"
start "dim_minutes = 0.02" "dim_percent = 30"      # 1.2 s
sleep 2.5
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 60 ] && ok "dimmed with the game held" || bad "not dimmed: brightness $(cat /sys/class/backlight/backlight1/brightness)"
touch /run/banana/game.held; sleep 0.6
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 200 ] && ok "back to 200 at the menu's press" || bad "still dim: $(cat /sys/class/backlight/backlight1/brightness)"
sleep 0.8; touch /run/banana/game.held; sleep 0.8
[ "$(cat /sys/class/backlight/backlight1/brightness)" = 200 ] && ok "not dimmed while the menu is used" || bad "dimmed 0.8 s after a press: $(cat /sys/class/backlight/backlight1/brightness)"
stop

echo "home-held 2. it sleeps on idle too; the wake leaves the game stopped (the menu lets it go on)"
echo 200 > /sys/class/backlight/backlight0/brightness; echo 200 > /sys/class/backlight/backlight1/brightness
: > /run/test/ui-signals
start "autosleep_minutes = 0.03" "dim_minutes = 0"     # 1.8 s
sleep 2.5
asleep && ok "asleep on idle with the game held" || bad "no idle sleep with the game held"
[ "$(hstate)" = T ] && ok "game stopped while asleep" || bad "game not stopped ($(hstate))"
wake
[ "$(hstate)" = T ] && ok "still stopped after the wake" || bad "the held game was let go ($(hstate))"
grep -q SIGUSR1 /run/test/ui-signals && ok "the menu told to draw again" || bad "no SIGUSR1 to the menu"
stop

echo "home-held 3. a shutdown lets the held game go, so it ends (its saves) and isn't waited for"
start "power_long_ms = 300"
[ "$(hstate)" = T ] || kill -STOP $HELD
ev $EV_KEY $KEY_POWER 1; sleep 0.6; ev $EV_KEY $KEY_POWER 0
sleep 0.5
[ -z "$(hstate)" ] || [ "$(hstate)" = Z ] && ok "the held game ended at the shutdown" || bad "the held game is still there ($(hstate))"
poweroff && ok "shutting down" || bad "no shutdown"
stop; POWEROFF=0
kill -CONT $HELD 2>/dev/null || true; kill $HELD 2>/dev/null || true
rm -f /run/banana/game.pid /run/banana/game.held
