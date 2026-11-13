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

# the game's control group, made up: banana-ui's gameFreeze writes 1 to its cgroup.freeze under the Home menu (a real
# frozen group can't be had in here; what is checked is that bananad writes 0 there)
CG=/sys/fs/cgroup/banana-game
[ -d /sys/fs/cgroup ] || mkdir -p /sys/fs/cgroup 2>/dev/null || true
mount -t tmpfs none /sys/fs/cgroup 2>/dev/null && mkdir -p $CG || CG=

echo "home-held 4. Power off after sleeping with the game held: thawed, then it saves and quits as one not held"
mkdir -p /data/drastic/savestates
export TEST_RESUME="/data/drastic/savestates/Peel Quest.resume.dss"
rm -f "$TEST_RESUME" /data/banana/resume-boot
game drastic "/data/roms/nds/Peel Quest.nds"
echo $GAME > /run/banana/game.held
[ -z "$CG" ] || echo 1 > $CG/cgroup.freeze
start "poweroff_after_sleep_minutes = 0.02"
ev $EV_KEY $KEY_POWER 1; ev $EV_KEY $KEY_POWER 0
asleep && ok "suspended with the game held" || bad "no suspend"
sleep 1.3; : > /sys/class/rtc/rtc0/wakealarm; wake
poweroff && ok "shut down" || bad "no shutdown"
if [ -n "$CG" ]; then
    [ "$(cat $CG/cgroup.freeze)" = 0 ] && ok "the game's control group thawed" || bad "cgroup.freeze still $(cat $CG/cgroup.freeze)"
fi
grep -qx USR1 /run/test/drastic.log 2>/dev/null && grep -qx saved /run/test/drastic.log && ok "drastic got SIGUSR1 and saved" || bad "drastic: $(cat /run/test/drastic.log 2>/dev/null)"
grep -qx emulator=superdrastic /data/banana/resume-boot 2>/dev/null && ok "resume-boot: superdrastic" || bad "resume-boot: $(cat /data/banana/resume-boot 2>/dev/null)"
stop; POWEROFF=0; export TEST_RESUME=
rm -f /run/banana/game.pid /run/banana/game.held /data/banana/resume-boot
echo 0 > /sys/class/backlight/backlight0/bl_power; echo 0 > /sys/class/backlight/backlight1/bl_power

echo "home-held 5. a game.held left behind (the menu died with its Home menu up) holds no later game"
setsid sleep 600 & HELD=$!; sleep 0.1
echo $HELD > /run/banana/game.pid; echo 999999 > /run/banana/game.held
echo 200 > /sys/class/backlight/backlight0/brightness; echo 200 > /sys/class/backlight/backlight1/brightness
start "autosleep_minutes = 0.03" "dim_minutes = 0"     # 1.8 s: the menu's idle sleep, not for a game being played
sleep 2.5
[ "$(cat /proc/$BD/wchan 2>/dev/null)" != wait_for_partner ] && ok "no idle sleep: the game counts as played" || { bad "asleep on idle as with the menu up"; wake; }
echo 1 > /run/test/lid; ev $EV_SW $SW_LID 1
asleep && ok "suspended on the lid" || bad "no suspend"
[ "$(hstate)" = T ] && ok "game stopped while asleep" || bad "game not stopped ($(hstate))"
echo 0 > /run/test/lid; wake
[ "$(hstate)" != T ] && ok "running again after the wake" || bad "left stopped as a held game ($(hstate))"
stop
kill -CONT $HELD 2>/dev/null || true; kill $HELD 2>/dev/null || true
rm -f /run/banana/game.pid /run/banana/game.held
[ -z "$CG" ] || umount /sys/fs/cgroup
